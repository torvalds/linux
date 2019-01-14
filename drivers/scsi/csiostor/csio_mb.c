/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_fc.h>

#include "csio_hw.h"
#include "csio_lnode.h"
#include "csio_rnode.h"
#include "csio_mb.h"
#include "csio_wr.h"

#define csio_mb_is_host_owner(__owner)		((__owner) == CSIO_MBOWNER_PL)

/* MB Command/Response Helpers */
/*
 * csio_mb_fw_retval - FW return value from a mailbox response.
 * @mbp: Mailbox structure
 *
 */
enum fw_retval
csio_mb_fw_retval(struct csio_mb *mbp)
{
	struct fw_cmd_hdr *hdr;

	hdr = (struct fw_cmd_hdr *)(mbp->mb);

	return FW_CMD_RETVAL_G(ntohl(hdr->lo));
}

/*
 * csio_mb_hello - FW HELLO command helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @m_mbox: Master mailbox number, if any.
 * @a_mbox: Mailbox number for asycn notifications.
 * @master: Device mastership.
 * @cbfn: Callback, if any.
 *
 */
void
csio_mb_hello(struct csio_hw *hw, struct csio_mb *mbp, uint32_t tmo,
	      uint32_t m_mbox, uint32_t a_mbox, enum csio_dev_master master,
	      void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_hello_cmd *cmdp = (struct fw_hello_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, tmo, hw, cbfn, 1);

	cmdp->op_to_write = htonl(FW_CMD_OP_V(FW_HELLO_CMD) |
				       FW_CMD_REQUEST_F | FW_CMD_WRITE_F);
	cmdp->retval_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));
	cmdp->err_to_clearinit = htonl(
		FW_HELLO_CMD_MASTERDIS_V(master == CSIO_MASTER_CANT)	|
		FW_HELLO_CMD_MASTERFORCE_V(master == CSIO_MASTER_MUST)	|
		FW_HELLO_CMD_MBMASTER_V(master == CSIO_MASTER_MUST ?
				m_mbox : FW_HELLO_CMD_MBMASTER_M)	|
		FW_HELLO_CMD_MBASYNCNOT_V(a_mbox) |
		FW_HELLO_CMD_STAGE_V(fw_hello_cmd_stage_os) |
		FW_HELLO_CMD_CLEARINIT_F);

}

/*
 * csio_mb_process_hello_rsp - FW HELLO response processing helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @retval: Mailbox return value from Firmware
 * @state: State that the function is in.
 * @mpfn: Master pfn
 *
 */
void
csio_mb_process_hello_rsp(struct csio_hw *hw, struct csio_mb *mbp,
			  enum fw_retval *retval, enum csio_dev_state *state,
			  uint8_t *mpfn)
{
	struct fw_hello_cmd *rsp = (struct fw_hello_cmd *)(mbp->mb);
	uint32_t value;

	*retval = FW_CMD_RETVAL_G(ntohl(rsp->retval_len16));

	if (*retval == FW_SUCCESS) {
		hw->fwrev = ntohl(rsp->fwrev);

		value = ntohl(rsp->err_to_clearinit);
		*mpfn = FW_HELLO_CMD_MBMASTER_G(value);

		if (value & FW_HELLO_CMD_INIT_F)
			*state = CSIO_DEV_STATE_INIT;
		else if (value & FW_HELLO_CMD_ERR_F)
			*state = CSIO_DEV_STATE_ERR;
		else
			*state = CSIO_DEV_STATE_UNINIT;
	}
}

/*
 * csio_mb_bye - FW BYE command helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @cbfn: Callback, if any.
 *
 */
void
csio_mb_bye(struct csio_hw *hw, struct csio_mb *mbp, uint32_t tmo,
	    void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_bye_cmd *cmdp = (struct fw_bye_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, tmo, hw, cbfn, 1);

	cmdp->op_to_write = htonl(FW_CMD_OP_V(FW_BYE_CMD) |
				       FW_CMD_REQUEST_F | FW_CMD_WRITE_F);
	cmdp->retval_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

}

/*
 * csio_mb_reset - FW RESET command helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @reset: Type of reset.
 * @cbfn: Callback, if any.
 *
 */
void
csio_mb_reset(struct csio_hw *hw, struct csio_mb *mbp, uint32_t tmo,
	      int reset, int halt,
	      void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_reset_cmd *cmdp = (struct fw_reset_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, tmo, hw, cbfn, 1);

	cmdp->op_to_write = htonl(FW_CMD_OP_V(FW_RESET_CMD) |
				  FW_CMD_REQUEST_F | FW_CMD_WRITE_F);
	cmdp->retval_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));
	cmdp->val = htonl(reset);
	cmdp->halt_pkd = htonl(halt);

}

/*
 * csio_mb_params - FW PARAMS command helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @tmo: Command timeout.
 * @pf: PF number.
 * @vf: VF number.
 * @nparams: Number of parameters
 * @params: Parameter mnemonic array.
 * @val: Parameter value array.
 * @wr: Write/Read PARAMS.
 * @cbfn: Callback, if any.
 *
 */
void
csio_mb_params(struct csio_hw *hw, struct csio_mb *mbp, uint32_t tmo,
	       unsigned int pf, unsigned int vf, unsigned int nparams,
	       const u32 *params, u32 *val, bool wr,
	       void (*cbfn)(struct csio_hw *, struct csio_mb *))
{
	uint32_t i;
	uint32_t temp_params = 0, temp_val = 0;
	struct fw_params_cmd *cmdp = (struct fw_params_cmd *)(mbp->mb);
	__be32 *p = &cmdp->param[0].mnem;

	CSIO_INIT_MBP(mbp, cmdp, tmo, hw, cbfn, 1);

	cmdp->op_to_vfn = htonl(FW_CMD_OP_V(FW_PARAMS_CMD)		|
				FW_CMD_REQUEST_F			|
				(wr ? FW_CMD_WRITE_F : FW_CMD_READ_F)	|
				FW_PARAMS_CMD_PFN_V(pf)			|
				FW_PARAMS_CMD_VFN_V(vf));
	cmdp->retval_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

	/* Write Params */
	if (wr) {
		while (nparams--) {
			temp_params = *params++;
			temp_val = *val++;

			*p++ = htonl(temp_params);
			*p++ = htonl(temp_val);
		}
	} else {
		for (i = 0; i < nparams; i++, p += 2) {
			temp_params = *params++;
			*p = htonl(temp_params);
		}
	}

}

/*
 * csio_mb_process_read_params_rsp - FW PARAMS response processing helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @retval: Mailbox return value from Firmware
 * @nparams: Number of parameters
 * @val: Parameter value array.
 *
 */
void
csio_mb_process_read_params_rsp(struct csio_hw *hw, struct csio_mb *mbp,
			   enum fw_retval *retval, unsigned int nparams,
			   u32 *val)
{
	struct fw_params_cmd *rsp = (struct fw_params_cmd *)(mbp->mb);
	uint32_t i;
	__be32 *p = &rsp->param[0].val;

	*retval = FW_CMD_RETVAL_G(ntohl(rsp->retval_len16));

	if (*retval == FW_SUCCESS)
		for (i = 0; i < nparams; i++, p += 2)
			*val++ = ntohl(*p);
}

/*
 * csio_mb_ldst - FW LDST command
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @tmo: timeout
 * @reg: register
 *
 */
void
csio_mb_ldst(struct csio_hw *hw, struct csio_mb *mbp, uint32_t tmo, int reg)
{
	struct fw_ldst_cmd *ldst_cmd = (struct fw_ldst_cmd *)(mbp->mb);
	CSIO_INIT_MBP(mbp, ldst_cmd, tmo, hw, NULL, 1);

	/*
	 * Construct and send the Firmware LDST Command to retrieve the
	 * specified PCI-E Configuration Space register.
	 */
	ldst_cmd->op_to_addrspace =
			htonl(FW_CMD_OP_V(FW_LDST_CMD)	|
			FW_CMD_REQUEST_F			|
			FW_CMD_READ_F			|
			FW_LDST_CMD_ADDRSPACE_V(FW_LDST_ADDRSPC_FUNC_PCIE));
	ldst_cmd->cycles_to_len16 = htonl(FW_LEN16(struct fw_ldst_cmd));
	ldst_cmd->u.pcie.select_naccess = FW_LDST_CMD_NACCESS_V(1);
	ldst_cmd->u.pcie.ctrl_to_fn =
		(FW_LDST_CMD_LC_F | FW_LDST_CMD_FN_V(hw->pfn));
	ldst_cmd->u.pcie.r = (uint8_t)reg;
}

/*
 *
 * csio_mb_caps_config - FW Read/Write Capabilities command helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @wr: Write if 1, Read if 0
 * @init: Turn on initiator mode.
 * @tgt: Turn on target mode.
 * @cofld:  If 1, Control Offload for FCoE
 * @cbfn: Callback, if any.
 *
 * This helper assumes that cmdp has MB payload from a previous CAPS
 * read command.
 */
void
csio_mb_caps_config(struct csio_hw *hw, struct csio_mb *mbp, uint32_t tmo,
		    bool wr, bool init, bool tgt, bool cofld,
		    void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_caps_config_cmd *cmdp =
				(struct fw_caps_config_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, tmo, hw, cbfn, wr ? 0 : 1);

	cmdp->op_to_write = htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
				  FW_CMD_REQUEST_F		|
				  (wr ? FW_CMD_WRITE_F : FW_CMD_READ_F));
	cmdp->cfvalid_to_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

	/* Read config */
	if (!wr)
		return;

	/* Write config */
	cmdp->fcoecaps = 0;

	if (cofld)
		cmdp->fcoecaps |= htons(FW_CAPS_CONFIG_FCOE_CTRL_OFLD);
	if (init)
		cmdp->fcoecaps |= htons(FW_CAPS_CONFIG_FCOE_INITIATOR);
	if (tgt)
		cmdp->fcoecaps |= htons(FW_CAPS_CONFIG_FCOE_TARGET);
}

/*
 * csio_mb_port- FW PORT command helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @tmo: COmmand timeout
 * @portid: Port ID to get/set info
 * @wr: Write/Read PORT information.
 * @fc: Flow control
 * @caps: Port capabilites to set.
 * @cbfn: Callback, if any.
 *
 */
void
csio_mb_port(struct csio_hw *hw, struct csio_mb *mbp, uint32_t tmo,
	     u8 portid, bool wr, uint32_t fc, uint16_t fw_caps,
	     void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_port_cmd *cmdp = (struct fw_port_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, tmo, hw, cbfn,  1);

	cmdp->op_to_portid = htonl(FW_CMD_OP_V(FW_PORT_CMD)		|
				   FW_CMD_REQUEST_F			|
				   (wr ? FW_CMD_EXEC_F : FW_CMD_READ_F)	|
				   FW_PORT_CMD_PORTID_V(portid));
	if (!wr) {
		cmdp->action_to_len16 = htonl(
			FW_PORT_CMD_ACTION_V(fw_caps == FW_CAPS16
			? FW_PORT_ACTION_GET_PORT_INFO
			: FW_PORT_ACTION_GET_PORT_INFO32) |
			FW_CMD_LEN16_V(sizeof(*cmdp) / 16));
		return;
	}

	/* Set port */
	cmdp->action_to_len16 = htonl(
			FW_PORT_CMD_ACTION_V(fw_caps == FW_CAPS16
			? FW_PORT_ACTION_L1_CFG
			: FW_PORT_ACTION_L1_CFG32) |
			FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

	if (fw_caps == FW_CAPS16)
		cmdp->u.l1cfg.rcap = cpu_to_be32(fwcaps32_to_caps16(fc));
	else
		cmdp->u.l1cfg32.rcap32 = cpu_to_be32(fc);
}

/*
 * csio_mb_process_read_port_rsp - FW PORT command response processing helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @retval: Mailbox return value from Firmware
 * @caps: port capabilities
 *
 */
void
csio_mb_process_read_port_rsp(struct csio_hw *hw, struct csio_mb *mbp,
			 enum fw_retval *retval, uint16_t fw_caps,
			 u32 *pcaps, u32 *acaps)
{
	struct fw_port_cmd *rsp = (struct fw_port_cmd *)(mbp->mb);

	*retval = FW_CMD_RETVAL_G(ntohl(rsp->action_to_len16));

	if (*retval == FW_SUCCESS) {
		if (fw_caps == FW_CAPS16) {
			*pcaps = fwcaps16_to_caps32(ntohs(rsp->u.info.pcap));
			*acaps = fwcaps16_to_caps32(ntohs(rsp->u.info.acap));
		} else {
			*pcaps = be32_to_cpu(rsp->u.info32.pcaps32);
			*acaps = be32_to_cpu(rsp->u.info32.acaps32);
		}
	}
}

/*
 * csio_mb_initialize - FW INITIALIZE command helper
 * @hw: The HW structure
 * @mbp: Mailbox structure
 * @tmo: COmmand timeout
 * @cbfn: Callback, if any.
 *
 */
void
csio_mb_initialize(struct csio_hw *hw, struct csio_mb *mbp, uint32_t tmo,
		   void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_initialize_cmd *cmdp = (struct fw_initialize_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, tmo, hw, cbfn, 1);

	cmdp->op_to_write = htonl(FW_CMD_OP_V(FW_INITIALIZE_CMD)	|
				  FW_CMD_REQUEST_F | FW_CMD_WRITE_F);
	cmdp->retval_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

}

/*
 * csio_mb_iq_alloc - Initializes the mailbox to allocate an
 *				Ingress DMA queue in the firmware.
 *
 * @hw: The hw structure
 * @mbp: Mailbox structure to initialize
 * @priv: Private object
 * @mb_tmo: Mailbox time-out period (in ms).
 * @iq_params: Ingress queue params needed for allocation.
 * @cbfn: The call-back function
 *
 *
 */
static void
csio_mb_iq_alloc(struct csio_hw *hw, struct csio_mb *mbp, void *priv,
		 uint32_t mb_tmo, struct csio_iq_params *iq_params,
		 void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_iq_cmd *cmdp = (struct fw_iq_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, priv, cbfn, 1);

	cmdp->op_to_vfn = htonl(FW_CMD_OP_V(FW_IQ_CMD)		|
				FW_CMD_REQUEST_F | FW_CMD_EXEC_F	|
				FW_IQ_CMD_PFN_V(iq_params->pfn)	|
				FW_IQ_CMD_VFN_V(iq_params->vfn));

	cmdp->alloc_to_len16 = htonl(FW_IQ_CMD_ALLOC_F		|
				FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

	cmdp->type_to_iqandstindex = htonl(
				FW_IQ_CMD_VIID_V(iq_params->viid)	|
				FW_IQ_CMD_TYPE_V(iq_params->type)	|
				FW_IQ_CMD_IQASYNCH_V(iq_params->iqasynch));

	cmdp->fl0size = htons(iq_params->fl0size);
	cmdp->fl0size = htons(iq_params->fl1size);

} /* csio_mb_iq_alloc */

/*
 * csio_mb_iq_write - Initializes the mailbox for writing into an
 *				Ingress DMA Queue.
 *
 * @hw: The HW structure
 * @mbp: Mailbox structure to initialize
 * @priv: Private object
 * @mb_tmo: Mailbox time-out period (in ms).
 * @cascaded_req: TRUE - if this request is cascased with iq-alloc request.
 * @iq_params: Ingress queue params needed for writing.
 * @cbfn: The call-back function
 *
 * NOTE: We OR relevant bits with cmdp->XXX, instead of just equating,
 * because this IQ write request can be cascaded with a previous
 * IQ alloc request, and we dont want to over-write the bits set by
 * that request. This logic will work even in a non-cascaded case, since the
 * cmdp structure is zeroed out by CSIO_INIT_MBP.
 */
static void
csio_mb_iq_write(struct csio_hw *hw, struct csio_mb *mbp, void *priv,
		 uint32_t mb_tmo, bool cascaded_req,
		 struct csio_iq_params *iq_params,
		 void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_iq_cmd *cmdp = (struct fw_iq_cmd *)(mbp->mb);

	uint32_t iq_start_stop = (iq_params->iq_start)	?
					FW_IQ_CMD_IQSTART_F :
					FW_IQ_CMD_IQSTOP_F;
	int relaxed = !(hw->flags & CSIO_HWF_ROOT_NO_RELAXED_ORDERING);

	/*
	 * If this IQ write is cascaded with IQ alloc request, do not
	 * re-initialize with 0's.
	 *
	 */
	if (!cascaded_req)
		CSIO_INIT_MBP(mbp, cmdp, mb_tmo, priv, cbfn, 1);

	cmdp->op_to_vfn |= htonl(FW_CMD_OP_V(FW_IQ_CMD)		|
				FW_CMD_REQUEST_F | FW_CMD_WRITE_F	|
				FW_IQ_CMD_PFN_V(iq_params->pfn)	|
				FW_IQ_CMD_VFN_V(iq_params->vfn));
	cmdp->alloc_to_len16 |= htonl(iq_start_stop |
				FW_CMD_LEN16_V(sizeof(*cmdp) / 16));
	cmdp->iqid |= htons(iq_params->iqid);
	cmdp->fl0id |= htons(iq_params->fl0id);
	cmdp->fl1id |= htons(iq_params->fl1id);
	cmdp->type_to_iqandstindex |= htonl(
			FW_IQ_CMD_IQANDST_V(iq_params->iqandst)	|
			FW_IQ_CMD_IQANUS_V(iq_params->iqanus)	|
			FW_IQ_CMD_IQANUD_V(iq_params->iqanud)	|
			FW_IQ_CMD_IQANDSTINDEX_V(iq_params->iqandstindex));
	cmdp->iqdroprss_to_iqesize |= htons(
			FW_IQ_CMD_IQPCIECH_V(iq_params->iqpciech)	|
			FW_IQ_CMD_IQDCAEN_V(iq_params->iqdcaen)		|
			FW_IQ_CMD_IQDCACPU_V(iq_params->iqdcacpu)	|
			FW_IQ_CMD_IQINTCNTTHRESH_V(iq_params->iqintcntthresh) |
			FW_IQ_CMD_IQCPRIO_V(iq_params->iqcprio)		|
			FW_IQ_CMD_IQESIZE_V(iq_params->iqesize));

	cmdp->iqsize |= htons(iq_params->iqsize);
	cmdp->iqaddr |= cpu_to_be64(iq_params->iqaddr);

	if (iq_params->type == 0) {
		cmdp->iqns_to_fl0congen |= htonl(
			FW_IQ_CMD_IQFLINTIQHSEN_V(iq_params->iqflintiqhsen)|
			FW_IQ_CMD_IQFLINTCONGEN_V(iq_params->iqflintcongen));
	}

	if (iq_params->fl0size && iq_params->fl0addr &&
	    (iq_params->fl0id != 0xFFFF)) {

		cmdp->iqns_to_fl0congen |= htonl(
			FW_IQ_CMD_FL0HOSTFCMODE_V(iq_params->fl0hostfcmode)|
			FW_IQ_CMD_FL0CPRIO_V(iq_params->fl0cprio)	|
			FW_IQ_CMD_FL0FETCHRO_V(relaxed)			|
			FW_IQ_CMD_FL0DATARO_V(relaxed)			|
			FW_IQ_CMD_FL0PADEN_V(iq_params->fl0paden)	|
			FW_IQ_CMD_FL0PACKEN_V(iq_params->fl0packen));
		cmdp->fl0dcaen_to_fl0cidxfthresh |= htons(
			FW_IQ_CMD_FL0DCAEN_V(iq_params->fl0dcaen)	|
			FW_IQ_CMD_FL0DCACPU_V(iq_params->fl0dcacpu)	|
			FW_IQ_CMD_FL0FBMIN_V(iq_params->fl0fbmin)	|
			FW_IQ_CMD_FL0FBMAX_V(iq_params->fl0fbmax)	|
			FW_IQ_CMD_FL0CIDXFTHRESH_V(iq_params->fl0cidxfthresh));
		cmdp->fl0size |= htons(iq_params->fl0size);
		cmdp->fl0addr |= cpu_to_be64(iq_params->fl0addr);
	}
} /* csio_mb_iq_write */

/*
 * csio_mb_iq_alloc_write - Initializes the mailbox for allocating an
 *				Ingress DMA Queue.
 *
 * @hw: The HW structure
 * @mbp: Mailbox structure to initialize
 * @priv: Private data.
 * @mb_tmo: Mailbox time-out period (in ms).
 * @iq_params: Ingress queue params needed for allocation & writing.
 * @cbfn: The call-back function
 *
 *
 */
void
csio_mb_iq_alloc_write(struct csio_hw *hw, struct csio_mb *mbp, void *priv,
		       uint32_t mb_tmo, struct csio_iq_params *iq_params,
		       void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	csio_mb_iq_alloc(hw, mbp, priv, mb_tmo, iq_params, cbfn);
	csio_mb_iq_write(hw, mbp, priv, mb_tmo, true, iq_params, cbfn);
} /* csio_mb_iq_alloc_write */

/*
 * csio_mb_iq_alloc_write_rsp - Process the allocation & writing
 *				of ingress DMA queue mailbox's response.
 *
 * @hw: The HW structure.
 * @mbp: Mailbox structure to initialize.
 * @retval: Firmware return value.
 * @iq_params: Ingress queue parameters, after allocation and write.
 *
 */
void
csio_mb_iq_alloc_write_rsp(struct csio_hw *hw, struct csio_mb *mbp,
			   enum fw_retval *ret_val,
			   struct csio_iq_params *iq_params)
{
	struct fw_iq_cmd *rsp = (struct fw_iq_cmd *)(mbp->mb);

	*ret_val = FW_CMD_RETVAL_G(ntohl(rsp->alloc_to_len16));
	if (*ret_val == FW_SUCCESS) {
		iq_params->physiqid = ntohs(rsp->physiqid);
		iq_params->iqid = ntohs(rsp->iqid);
		iq_params->fl0id = ntohs(rsp->fl0id);
		iq_params->fl1id = ntohs(rsp->fl1id);
	} else {
		iq_params->physiqid = iq_params->iqid =
		iq_params->fl0id = iq_params->fl1id = 0;
	}
} /* csio_mb_iq_alloc_write_rsp */

/*
 * csio_mb_iq_free - Initializes the mailbox for freeing a
 *				specified Ingress DMA Queue.
 *
 * @hw: The HW structure
 * @mbp: Mailbox structure to initialize
 * @priv: Private data
 * @mb_tmo: Mailbox time-out period (in ms).
 * @iq_params: Parameters of ingress queue, that is to be freed.
 * @cbfn: The call-back function
 *
 *
 */
void
csio_mb_iq_free(struct csio_hw *hw, struct csio_mb *mbp, void *priv,
		uint32_t mb_tmo, struct csio_iq_params *iq_params,
		void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_iq_cmd *cmdp = (struct fw_iq_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, priv, cbfn, 1);

	cmdp->op_to_vfn = htonl(FW_CMD_OP_V(FW_IQ_CMD)		|
				FW_CMD_REQUEST_F | FW_CMD_EXEC_F	|
				FW_IQ_CMD_PFN_V(iq_params->pfn)	|
				FW_IQ_CMD_VFN_V(iq_params->vfn));
	cmdp->alloc_to_len16 = htonl(FW_IQ_CMD_FREE_F		|
				FW_CMD_LEN16_V(sizeof(*cmdp) / 16));
	cmdp->type_to_iqandstindex = htonl(FW_IQ_CMD_TYPE_V(iq_params->type));

	cmdp->iqid = htons(iq_params->iqid);
	cmdp->fl0id = htons(iq_params->fl0id);
	cmdp->fl1id = htons(iq_params->fl1id);

} /* csio_mb_iq_free */

/*
 * csio_mb_eq_ofld_alloc - Initializes the mailbox for allocating
 *				an offload-egress queue.
 *
 * @hw: The HW  structure
 * @mbp: Mailbox structure to initialize
 * @priv: Private data
 * @mb_tmo: Mailbox time-out period (in ms).
 * @eq_ofld_params: (Offload) Egress queue parameters.
 * @cbfn: The call-back function
 *
 *
 */
static void
csio_mb_eq_ofld_alloc(struct csio_hw *hw, struct csio_mb *mbp, void *priv,
		uint32_t mb_tmo, struct csio_eq_params *eq_ofld_params,
		void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_eq_ofld_cmd *cmdp = (struct fw_eq_ofld_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, priv, cbfn, 1);
	cmdp->op_to_vfn = htonl(FW_CMD_OP_V(FW_EQ_OFLD_CMD)		|
				FW_CMD_REQUEST_F | FW_CMD_EXEC_F	|
				FW_EQ_OFLD_CMD_PFN_V(eq_ofld_params->pfn) |
				FW_EQ_OFLD_CMD_VFN_V(eq_ofld_params->vfn));
	cmdp->alloc_to_len16 = htonl(FW_EQ_OFLD_CMD_ALLOC_F	|
				FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

} /* csio_mb_eq_ofld_alloc */

/*
 * csio_mb_eq_ofld_write - Initializes the mailbox for writing
 *				an alloacted offload-egress queue.
 *
 * @hw: The HW structure
 * @mbp: Mailbox structure to initialize
 * @priv: Private data
 * @mb_tmo: Mailbox time-out period (in ms).
 * @cascaded_req: TRUE - if this request is cascased with Eq-alloc request.
 * @eq_ofld_params: (Offload) Egress queue parameters.
 * @cbfn: The call-back function
 *
 *
 * NOTE: We OR relevant bits with cmdp->XXX, instead of just equating,
 * because this EQ write request can be cascaded with a previous
 * EQ alloc request, and we dont want to over-write the bits set by
 * that request. This logic will work even in a non-cascaded case, since the
 * cmdp structure is zeroed out by CSIO_INIT_MBP.
 */
static void
csio_mb_eq_ofld_write(struct csio_hw *hw, struct csio_mb *mbp, void *priv,
		      uint32_t mb_tmo, bool cascaded_req,
		      struct csio_eq_params *eq_ofld_params,
		      void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_eq_ofld_cmd *cmdp = (struct fw_eq_ofld_cmd *)(mbp->mb);

	uint32_t eq_start_stop = (eq_ofld_params->eqstart)	?
				FW_EQ_OFLD_CMD_EQSTART_F :
				FW_EQ_OFLD_CMD_EQSTOP_F;

	/*
	 * If this EQ write is cascaded with EQ alloc request, do not
	 * re-initialize with 0's.
	 *
	 */
	if (!cascaded_req)
		CSIO_INIT_MBP(mbp, cmdp, mb_tmo, priv, cbfn, 1);

	cmdp->op_to_vfn |= htonl(FW_CMD_OP_V(FW_EQ_OFLD_CMD)	|
				FW_CMD_REQUEST_F | FW_CMD_WRITE_F	|
				FW_EQ_OFLD_CMD_PFN_V(eq_ofld_params->pfn) |
				FW_EQ_OFLD_CMD_VFN_V(eq_ofld_params->vfn));
	cmdp->alloc_to_len16 |= htonl(eq_start_stop		|
				      FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

	cmdp->eqid_pkd |= htonl(FW_EQ_OFLD_CMD_EQID_V(eq_ofld_params->eqid));

	cmdp->fetchszm_to_iqid |= htonl(
		FW_EQ_OFLD_CMD_HOSTFCMODE_V(eq_ofld_params->hostfcmode)	|
		FW_EQ_OFLD_CMD_CPRIO_V(eq_ofld_params->cprio)		|
		FW_EQ_OFLD_CMD_PCIECHN_V(eq_ofld_params->pciechn)	|
		FW_EQ_OFLD_CMD_IQID_V(eq_ofld_params->iqid));

	cmdp->dcaen_to_eqsize |= htonl(
		FW_EQ_OFLD_CMD_DCAEN_V(eq_ofld_params->dcaen)		|
		FW_EQ_OFLD_CMD_DCACPU_V(eq_ofld_params->dcacpu)		|
		FW_EQ_OFLD_CMD_FBMIN_V(eq_ofld_params->fbmin)		|
		FW_EQ_OFLD_CMD_FBMAX_V(eq_ofld_params->fbmax)		|
		FW_EQ_OFLD_CMD_CIDXFTHRESHO_V(eq_ofld_params->cidxfthresho) |
		FW_EQ_OFLD_CMD_CIDXFTHRESH_V(eq_ofld_params->cidxfthresh) |
		FW_EQ_OFLD_CMD_EQSIZE_V(eq_ofld_params->eqsize));

	cmdp->eqaddr |= cpu_to_be64(eq_ofld_params->eqaddr);

} /* csio_mb_eq_ofld_write */

/*
 * csio_mb_eq_ofld_alloc_write - Initializes the mailbox for allocation
 *				writing into an Engress DMA Queue.
 *
 * @hw: The HW structure
 * @mbp: Mailbox structure to initialize
 * @priv: Private data.
 * @mb_tmo: Mailbox time-out period (in ms).
 * @eq_ofld_params: (Offload) Egress queue parameters.
 * @cbfn: The call-back function
 *
 *
 */
void
csio_mb_eq_ofld_alloc_write(struct csio_hw *hw, struct csio_mb *mbp,
			    void *priv, uint32_t mb_tmo,
			    struct csio_eq_params *eq_ofld_params,
			    void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	csio_mb_eq_ofld_alloc(hw, mbp, priv, mb_tmo, eq_ofld_params, cbfn);
	csio_mb_eq_ofld_write(hw, mbp, priv, mb_tmo, true,
			      eq_ofld_params, cbfn);
} /* csio_mb_eq_ofld_alloc_write */

/*
 * csio_mb_eq_ofld_alloc_write_rsp - Process the allocation
 *				& write egress DMA queue mailbox's response.
 *
 * @hw: The HW structure.
 * @mbp: Mailbox structure to initialize.
 * @retval: Firmware return value.
 * @eq_ofld_params: (Offload) Egress queue parameters.
 *
 */
void
csio_mb_eq_ofld_alloc_write_rsp(struct csio_hw *hw,
				struct csio_mb *mbp, enum fw_retval *ret_val,
				struct csio_eq_params *eq_ofld_params)
{
	struct fw_eq_ofld_cmd *rsp = (struct fw_eq_ofld_cmd *)(mbp->mb);

	*ret_val = FW_CMD_RETVAL_G(ntohl(rsp->alloc_to_len16));

	if (*ret_val == FW_SUCCESS) {
		eq_ofld_params->eqid = FW_EQ_OFLD_CMD_EQID_G(
						ntohl(rsp->eqid_pkd));
		eq_ofld_params->physeqid = FW_EQ_OFLD_CMD_PHYSEQID_G(
						ntohl(rsp->physeqid_pkd));
	} else
		eq_ofld_params->eqid = 0;

} /* csio_mb_eq_ofld_alloc_write_rsp */

/*
 * csio_mb_eq_ofld_free - Initializes the mailbox for freeing a
 *				specified Engress DMA Queue.
 *
 * @hw: The HW structure
 * @mbp: Mailbox structure to initialize
 * @priv: Private data area.
 * @mb_tmo: Mailbox time-out period (in ms).
 * @eq_ofld_params: (Offload) Egress queue parameters, that is to be freed.
 * @cbfn: The call-back function
 *
 *
 */
void
csio_mb_eq_ofld_free(struct csio_hw *hw, struct csio_mb *mbp, void *priv,
		     uint32_t mb_tmo, struct csio_eq_params *eq_ofld_params,
		     void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_eq_ofld_cmd *cmdp = (struct fw_eq_ofld_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, priv, cbfn, 1);

	cmdp->op_to_vfn = htonl(FW_CMD_OP_V(FW_EQ_OFLD_CMD)	|
				FW_CMD_REQUEST_F | FW_CMD_EXEC_F	|
				FW_EQ_OFLD_CMD_PFN_V(eq_ofld_params->pfn) |
				FW_EQ_OFLD_CMD_VFN_V(eq_ofld_params->vfn));
	cmdp->alloc_to_len16 = htonl(FW_EQ_OFLD_CMD_FREE_F |
				FW_CMD_LEN16_V(sizeof(*cmdp) / 16));
	cmdp->eqid_pkd = htonl(FW_EQ_OFLD_CMD_EQID_V(eq_ofld_params->eqid));

} /* csio_mb_eq_ofld_free */

/*
 * csio_write_fcoe_link_cond_init_mb - Initialize Mailbox to write FCoE link
 *				 condition.
 *
 * @ln: The Lnode structure
 * @mbp: Mailbox structure to initialize
 * @mb_tmo: Mailbox time-out period (in ms).
 * @cbfn: The call back function.
 *
 *
 */
void
csio_write_fcoe_link_cond_init_mb(struct csio_lnode *ln, struct csio_mb *mbp,
			uint32_t mb_tmo, uint8_t port_id, uint32_t sub_opcode,
			uint8_t cos, bool link_status, uint32_t fcfi,
			void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_fcoe_link_cmd *cmdp =
				(struct fw_fcoe_link_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, ln, cbfn, 1);

	cmdp->op_to_portid = htonl((
			FW_CMD_OP_V(FW_FCOE_LINK_CMD)		|
			FW_CMD_REQUEST_F				|
			FW_CMD_WRITE_F				|
			FW_FCOE_LINK_CMD_PORTID(port_id)));
	cmdp->sub_opcode_fcfi = htonl(
			FW_FCOE_LINK_CMD_SUB_OPCODE(sub_opcode)	|
			FW_FCOE_LINK_CMD_FCFI(fcfi));
	cmdp->lstatus = link_status;
	cmdp->retval_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

} /* csio_write_fcoe_link_cond_init_mb */

/*
 * csio_fcoe_read_res_info_init_mb - Initializes the mailbox for reading FCoE
 *				resource information(FW_GET_RES_INFO_CMD).
 *
 * @hw: The HW structure
 * @mbp: Mailbox structure to initialize
 * @mb_tmo: Mailbox time-out period (in ms).
 * @cbfn: The call-back function
 *
 *
 */
void
csio_fcoe_read_res_info_init_mb(struct csio_hw *hw, struct csio_mb *mbp,
			uint32_t mb_tmo,
			void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_fcoe_res_info_cmd *cmdp =
			(struct fw_fcoe_res_info_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, hw, cbfn, 1);

	cmdp->op_to_read = htonl((FW_CMD_OP_V(FW_FCOE_RES_INFO_CMD)	|
				  FW_CMD_REQUEST_F			|
				  FW_CMD_READ_F));

	cmdp->retval_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

} /* csio_fcoe_read_res_info_init_mb */

/*
 * csio_fcoe_vnp_alloc_init_mb - Initializes the mailbox for allocating VNP
 *				in the firmware (FW_FCOE_VNP_CMD).
 *
 * @ln: The Lnode structure.
 * @mbp: Mailbox structure to initialize.
 * @mb_tmo: Mailbox time-out period (in ms).
 * @fcfi: FCF Index.
 * @vnpi: vnpi
 * @iqid: iqid
 * @vnport_wwnn: vnport WWNN
 * @vnport_wwpn: vnport WWPN
 * @cbfn: The call-back function.
 *
 *
 */
void
csio_fcoe_vnp_alloc_init_mb(struct csio_lnode *ln, struct csio_mb *mbp,
		uint32_t mb_tmo, uint32_t fcfi, uint32_t vnpi, uint16_t iqid,
		uint8_t vnport_wwnn[8],	uint8_t vnport_wwpn[8],
		void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_fcoe_vnp_cmd *cmdp =
			(struct fw_fcoe_vnp_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, ln, cbfn, 1);

	cmdp->op_to_fcfi = htonl((FW_CMD_OP_V(FW_FCOE_VNP_CMD)		|
				  FW_CMD_REQUEST_F			|
				  FW_CMD_EXEC_F				|
				  FW_FCOE_VNP_CMD_FCFI(fcfi)));

	cmdp->alloc_to_len16 = htonl(FW_FCOE_VNP_CMD_ALLOC		|
				     FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

	cmdp->gen_wwn_to_vnpi = htonl(FW_FCOE_VNP_CMD_VNPI(vnpi));

	cmdp->iqid = htons(iqid);

	if (!wwn_to_u64(vnport_wwnn) && !wwn_to_u64(vnport_wwpn))
		cmdp->gen_wwn_to_vnpi |= htonl(FW_FCOE_VNP_CMD_GEN_WWN);

	if (vnport_wwnn)
		memcpy(cmdp->vnport_wwnn, vnport_wwnn, 8);
	if (vnport_wwpn)
		memcpy(cmdp->vnport_wwpn, vnport_wwpn, 8);

} /* csio_fcoe_vnp_alloc_init_mb */

/*
 * csio_fcoe_vnp_read_init_mb - Prepares VNP read cmd.
 * @ln: The Lnode structure.
 * @mbp: Mailbox structure to initialize.
 * @mb_tmo: Mailbox time-out period (in ms).
 * @fcfi: FCF Index.
 * @vnpi: vnpi
 * @cbfn: The call-back handler.
 */
void
csio_fcoe_vnp_read_init_mb(struct csio_lnode *ln, struct csio_mb *mbp,
		uint32_t mb_tmo, uint32_t fcfi, uint32_t vnpi,
		void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_fcoe_vnp_cmd *cmdp =
			(struct fw_fcoe_vnp_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, ln, cbfn, 1);
	cmdp->op_to_fcfi = htonl(FW_CMD_OP_V(FW_FCOE_VNP_CMD)	|
				 FW_CMD_REQUEST_F			|
				 FW_CMD_READ_F			|
				 FW_FCOE_VNP_CMD_FCFI(fcfi));
	cmdp->alloc_to_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));
	cmdp->gen_wwn_to_vnpi = htonl(FW_FCOE_VNP_CMD_VNPI(vnpi));
}

/*
 * csio_fcoe_vnp_free_init_mb - Initializes the mailbox for freeing an
 *			alloacted VNP in the firmware (FW_FCOE_VNP_CMD).
 *
 * @ln: The Lnode structure.
 * @mbp: Mailbox structure to initialize.
 * @mb_tmo: Mailbox time-out period (in ms).
 * @fcfi: FCF flow id
 * @vnpi: VNP flow id
 * @cbfn: The call-back function.
 * Return: None
 */
void
csio_fcoe_vnp_free_init_mb(struct csio_lnode *ln, struct csio_mb *mbp,
		uint32_t mb_tmo, uint32_t fcfi, uint32_t vnpi,
		void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_fcoe_vnp_cmd *cmdp =
			(struct fw_fcoe_vnp_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, ln, cbfn, 1);

	cmdp->op_to_fcfi = htonl(FW_CMD_OP_V(FW_FCOE_VNP_CMD)	|
				 FW_CMD_REQUEST_F			|
				 FW_CMD_EXEC_F			|
				 FW_FCOE_VNP_CMD_FCFI(fcfi));
	cmdp->alloc_to_len16 = htonl(FW_FCOE_VNP_CMD_FREE	|
				     FW_CMD_LEN16_V(sizeof(*cmdp) / 16));
	cmdp->gen_wwn_to_vnpi = htonl(FW_FCOE_VNP_CMD_VNPI(vnpi));
}

/*
 * csio_fcoe_read_fcf_init_mb - Initializes the mailbox to read the
 *				FCF records.
 *
 * @ln: The Lnode structure
 * @mbp: Mailbox structure to initialize
 * @mb_tmo: Mailbox time-out period (in ms).
 * @fcf_params: FC-Forwarder parameters.
 * @cbfn: The call-back function
 *
 *
 */
void
csio_fcoe_read_fcf_init_mb(struct csio_lnode *ln, struct csio_mb *mbp,
		uint32_t mb_tmo, uint32_t portid, uint32_t fcfi,
		void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct fw_fcoe_fcf_cmd *cmdp =
			(struct fw_fcoe_fcf_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, ln, cbfn, 1);

	cmdp->op_to_fcfi = htonl(FW_CMD_OP_V(FW_FCOE_FCF_CMD)	|
				 FW_CMD_REQUEST_F			|
				 FW_CMD_READ_F			|
				 FW_FCOE_FCF_CMD_FCFI(fcfi));
	cmdp->retval_len16 = htonl(FW_CMD_LEN16_V(sizeof(*cmdp) / 16));

} /* csio_fcoe_read_fcf_init_mb */

void
csio_fcoe_read_portparams_init_mb(struct csio_hw *hw, struct csio_mb *mbp,
				uint32_t mb_tmo,
				struct fw_fcoe_port_cmd_params *portparams,
				void (*cbfn)(struct csio_hw *,
					     struct csio_mb *))
{
	struct fw_fcoe_stats_cmd *cmdp = (struct fw_fcoe_stats_cmd *)(mbp->mb);

	CSIO_INIT_MBP(mbp, cmdp, mb_tmo, hw, cbfn, 1);
	mbp->mb_size = 64;

	cmdp->op_to_flowid = htonl(FW_CMD_OP_V(FW_FCOE_STATS_CMD)         |
				   FW_CMD_REQUEST_F | FW_CMD_READ_F);
	cmdp->free_to_len16 = htonl(FW_CMD_LEN16_V(CSIO_MAX_MB_SIZE/16));

	cmdp->u.ctl.nstats_port = FW_FCOE_STATS_CMD_NSTATS(portparams->nstats) |
				  FW_FCOE_STATS_CMD_PORT(portparams->portid);

	cmdp->u.ctl.port_valid_ix = FW_FCOE_STATS_CMD_IX(portparams->idx)    |
				    FW_FCOE_STATS_CMD_PORT_VALID;

} /* csio_fcoe_read_portparams_init_mb */

void
csio_mb_process_portparams_rsp(struct csio_hw *hw,
				struct csio_mb *mbp,
				enum fw_retval *retval,
				struct fw_fcoe_port_cmd_params *portparams,
				struct fw_fcoe_port_stats *portstats)
{
	struct fw_fcoe_stats_cmd *rsp = (struct fw_fcoe_stats_cmd *)(mbp->mb);
	struct fw_fcoe_port_stats stats;
	uint8_t *src;
	uint8_t *dst;

	*retval = FW_CMD_RETVAL_G(ntohl(rsp->free_to_len16));

	memset(&stats, 0, sizeof(struct fw_fcoe_port_stats));

	if (*retval == FW_SUCCESS) {
		dst = (uint8_t *)(&stats) + ((portparams->idx - 1) * 8);
		src = (uint8_t *)rsp + (CSIO_STATS_OFFSET * 8);
		memcpy(dst, src, (portparams->nstats * 8));
		if (portparams->idx == 1) {
			/* Get the first 6 flits from the Mailbox */
			portstats->tx_bcast_bytes = stats.tx_bcast_bytes;
			portstats->tx_bcast_frames = stats.tx_bcast_frames;
			portstats->tx_mcast_bytes = stats.tx_mcast_bytes;
			portstats->tx_mcast_frames = stats.tx_mcast_frames;
			portstats->tx_ucast_bytes = stats.tx_ucast_bytes;
			portstats->tx_ucast_frames = stats.tx_ucast_frames;
		}
		if (portparams->idx == 7) {
			/* Get the second 6 flits from the Mailbox */
			portstats->tx_drop_frames = stats.tx_drop_frames;
			portstats->tx_offload_bytes = stats.tx_offload_bytes;
			portstats->tx_offload_frames = stats.tx_offload_frames;
#if 0
			portstats->rx_pf_bytes = stats.rx_pf_bytes;
			portstats->rx_pf_frames	= stats.rx_pf_frames;
#endif
			portstats->rx_bcast_bytes = stats.rx_bcast_bytes;
			portstats->rx_bcast_frames = stats.rx_bcast_frames;
			portstats->rx_mcast_bytes = stats.rx_mcast_bytes;
		}
		if (portparams->idx == 13) {
			/* Get the last 4 flits from the Mailbox */
			portstats->rx_mcast_frames = stats.rx_mcast_frames;
			portstats->rx_ucast_bytes = stats.rx_ucast_bytes;
			portstats->rx_ucast_frames = stats.rx_ucast_frames;
			portstats->rx_err_frames = stats.rx_err_frames;
		}
	}
}

/* Entry points/APIs for MB module					     */
/*
 * csio_mb_intr_enable - Enable Interrupts from mailboxes.
 * @hw: The HW structure
 *
 * Enables CIM interrupt bit in appropriate INT_ENABLE registers.
 */
void
csio_mb_intr_enable(struct csio_hw *hw)
{
	csio_wr_reg32(hw, MBMSGRDYINTEN_F, MYPF_REG(CIM_PF_HOST_INT_ENABLE_A));
	csio_rd_reg32(hw, MYPF_REG(CIM_PF_HOST_INT_ENABLE_A));
}

/*
 * csio_mb_intr_disable - Disable Interrupts from mailboxes.
 * @hw: The HW structure
 *
 * Disable bit in HostInterruptEnable CIM register.
 */
void
csio_mb_intr_disable(struct csio_hw *hw)
{
	csio_wr_reg32(hw, MBMSGRDYINTEN_V(0),
		      MYPF_REG(CIM_PF_HOST_INT_ENABLE_A));
	csio_rd_reg32(hw, MYPF_REG(CIM_PF_HOST_INT_ENABLE_A));
}

static void
csio_mb_dump_fw_dbg(struct csio_hw *hw, __be64 *cmd)
{
	struct fw_debug_cmd *dbg = (struct fw_debug_cmd *)cmd;

	if ((FW_DEBUG_CMD_TYPE_G(ntohl(dbg->op_type))) == 1) {
		csio_info(hw, "FW print message:\n");
		csio_info(hw, "\tdebug->dprtstridx = %d\n",
			    ntohs(dbg->u.prt.dprtstridx));
		csio_info(hw, "\tdebug->dprtstrparam0 = 0x%x\n",
			    ntohl(dbg->u.prt.dprtstrparam0));
		csio_info(hw, "\tdebug->dprtstrparam1 = 0x%x\n",
			    ntohl(dbg->u.prt.dprtstrparam1));
		csio_info(hw, "\tdebug->dprtstrparam2 = 0x%x\n",
			    ntohl(dbg->u.prt.dprtstrparam2));
		csio_info(hw, "\tdebug->dprtstrparam3 = 0x%x\n",
			    ntohl(dbg->u.prt.dprtstrparam3));
	} else {
		/* This is a FW assertion */
		csio_fatal(hw, "FW assertion at %.16s:%u, val0 %#x, val1 %#x\n",
			    dbg->u.assert.filename_0_7,
			    ntohl(dbg->u.assert.line),
			    ntohl(dbg->u.assert.x),
			    ntohl(dbg->u.assert.y));
	}
}

static void
csio_mb_debug_cmd_handler(struct csio_hw *hw)
{
	int i;
	__be64 cmd[CSIO_MB_MAX_REGS];
	uint32_t ctl_reg = PF_REG(hw->pfn, CIM_PF_MAILBOX_CTRL_A);
	uint32_t data_reg = PF_REG(hw->pfn, CIM_PF_MAILBOX_DATA_A);
	int size = sizeof(struct fw_debug_cmd);

	/* Copy mailbox data */
	for (i = 0; i < size; i += 8)
		cmd[i / 8] = cpu_to_be64(csio_rd_reg64(hw, data_reg + i));

	csio_mb_dump_fw_dbg(hw, cmd);

	/* Notify FW of mailbox by setting owner as UP */
	csio_wr_reg32(hw, MBMSGVALID_F | MBINTREQ_F |
		      MBOWNER_V(CSIO_MBOWNER_FW), ctl_reg);

	csio_rd_reg32(hw, ctl_reg);
	wmb();
}

/*
 * csio_mb_issue - generic routine for issuing Mailbox commands.
 * @hw: The HW structure
 * @mbp: Mailbox command to issue
 *
 *  Caller should hold hw lock across this call.
 */
int
csio_mb_issue(struct csio_hw *hw, struct csio_mb *mbp)
{
	uint32_t owner, ctl;
	int i;
	uint32_t ii;
	__be64 *cmd = mbp->mb;
	__be64 hdr;
	struct csio_mbm	*mbm = &hw->mbm;
	uint32_t ctl_reg = PF_REG(hw->pfn, CIM_PF_MAILBOX_CTRL_A);
	uint32_t data_reg = PF_REG(hw->pfn, CIM_PF_MAILBOX_DATA_A);
	int size = mbp->mb_size;
	int rv = -EINVAL;
	struct fw_cmd_hdr *fw_hdr;

	/* Determine mode */
	if (mbp->mb_cbfn == NULL) {
		/* Need to issue/get results in the same context */
		if (mbp->tmo < CSIO_MB_POLL_FREQ) {
			csio_err(hw, "Invalid tmo: 0x%x\n", mbp->tmo);
			goto error_out;
		}
	} else if (!csio_is_host_intr_enabled(hw) ||
		   !csio_is_hw_intr_enabled(hw)) {
		csio_err(hw, "Cannot issue mailbox in interrupt mode 0x%x\n",
			 *((uint8_t *)mbp->mb));
			goto error_out;
	}

	if (mbm->mcurrent != NULL) {
		/* Queue mbox cmd, if another mbox cmd is active */
		if (mbp->mb_cbfn == NULL) {
			rv = -EBUSY;
			csio_dbg(hw, "Couldn't own Mailbox %x op:0x%x\n",
				    hw->pfn, *((uint8_t *)mbp->mb));

			goto error_out;
		} else {
			list_add_tail(&mbp->list, &mbm->req_q);
			CSIO_INC_STATS(mbm, n_activeq);

			return 0;
		}
	}

	/* Now get ownership of mailbox */
	owner = MBOWNER_G(csio_rd_reg32(hw, ctl_reg));

	if (!csio_mb_is_host_owner(owner)) {

		for (i = 0; (owner == CSIO_MBOWNER_NONE) && (i < 3); i++)
			owner = MBOWNER_G(csio_rd_reg32(hw, ctl_reg));
		/*
		 * Mailbox unavailable. In immediate mode, fail the command.
		 * In other modes, enqueue the request.
		 */
		if (!csio_mb_is_host_owner(owner)) {
			if (mbp->mb_cbfn == NULL) {
				rv = owner ? -EBUSY : -ETIMEDOUT;

				csio_dbg(hw,
					 "Couldn't own Mailbox %x op:0x%x "
					 "owner:%x\n",
					 hw->pfn, *((uint8_t *)mbp->mb), owner);
				goto error_out;
			} else {
				if (mbm->mcurrent == NULL) {
					csio_err(hw,
						 "Couldn't own Mailbox %x "
						 "op:0x%x owner:%x\n",
						 hw->pfn, *((uint8_t *)mbp->mb),
						 owner);
					csio_err(hw,
						 "No outstanding driver"
						 " mailbox as well\n");
					goto error_out;
				}
			}
		}
	}

	/* Mailbox is available, copy mailbox data into it */
	for (i = 0; i < size; i += 8) {
		csio_wr_reg64(hw, be64_to_cpu(*cmd), data_reg + i);
		cmd++;
	}

	CSIO_DUMP_MB(hw, hw->pfn, data_reg);

	/* Start completion timers in non-immediate modes and notify FW */
	if (mbp->mb_cbfn != NULL) {
		mbm->mcurrent = mbp;
		mod_timer(&mbm->timer, jiffies + msecs_to_jiffies(mbp->tmo));
		csio_wr_reg32(hw, MBMSGVALID_F | MBINTREQ_F |
			      MBOWNER_V(CSIO_MBOWNER_FW), ctl_reg);
	} else
		csio_wr_reg32(hw, MBMSGVALID_F | MBOWNER_V(CSIO_MBOWNER_FW),
			      ctl_reg);

	/* Flush posted writes */
	csio_rd_reg32(hw, ctl_reg);
	wmb();

	CSIO_INC_STATS(mbm, n_req);

	if (mbp->mb_cbfn)
		return 0;

	/* Poll for completion in immediate mode */
	cmd = mbp->mb;

	for (ii = 0; ii < mbp->tmo; ii += CSIO_MB_POLL_FREQ) {
		mdelay(CSIO_MB_POLL_FREQ);

		/* Check for response */
		ctl = csio_rd_reg32(hw, ctl_reg);
		if (csio_mb_is_host_owner(MBOWNER_G(ctl))) {

			if (!(ctl & MBMSGVALID_F)) {
				csio_wr_reg32(hw, 0, ctl_reg);
				continue;
			}

			CSIO_DUMP_MB(hw, hw->pfn, data_reg);

			hdr = cpu_to_be64(csio_rd_reg64(hw, data_reg));
			fw_hdr = (struct fw_cmd_hdr *)&hdr;

			switch (FW_CMD_OP_G(ntohl(fw_hdr->hi))) {
			case FW_DEBUG_CMD:
				csio_mb_debug_cmd_handler(hw);
				continue;
			}

			/* Copy response */
			for (i = 0; i < size; i += 8)
				*cmd++ = cpu_to_be64(csio_rd_reg64
							  (hw, data_reg + i));
			csio_wr_reg32(hw, 0, ctl_reg);

			if (csio_mb_fw_retval(mbp) != FW_SUCCESS)
				CSIO_INC_STATS(mbm, n_err);

			CSIO_INC_STATS(mbm, n_rsp);
			return 0;
		}
	}

	CSIO_INC_STATS(mbm, n_tmo);

	csio_err(hw, "Mailbox %x op:0x%x timed out!\n",
		 hw->pfn, *((uint8_t *)cmd));

	return -ETIMEDOUT;

error_out:
	CSIO_INC_STATS(mbm, n_err);
	return rv;
}

/*
 * csio_mb_completions - Completion handler for Mailbox commands
 * @hw: The HW structure
 * @cbfn_q: Completion queue.
 *
 */
void
csio_mb_completions(struct csio_hw *hw, struct list_head *cbfn_q)
{
	struct csio_mb *mbp;
	struct csio_mbm *mbm = &hw->mbm;
	enum fw_retval rv;

	while (!list_empty(cbfn_q)) {
		mbp = list_first_entry(cbfn_q, struct csio_mb, list);
		list_del_init(&mbp->list);

		rv = csio_mb_fw_retval(mbp);
		if ((rv != FW_SUCCESS) && (rv != FW_HOSTERROR))
			CSIO_INC_STATS(mbm, n_err);
		else if (rv != FW_HOSTERROR)
			CSIO_INC_STATS(mbm, n_rsp);

		if (mbp->mb_cbfn)
			mbp->mb_cbfn(hw, mbp);
	}
}

static void
csio_mb_portmod_changed(struct csio_hw *hw, uint8_t port_id)
{
	static char *mod_str[] = {
		NULL, "LR", "SR", "ER", "TWINAX", "active TWINAX", "LRM"
	};

	struct csio_pport *port = &hw->pport[port_id];

	if (port->mod_type == FW_PORT_MOD_TYPE_NONE)
		csio_info(hw, "Port:%d - port module unplugged\n", port_id);
	else if (port->mod_type < ARRAY_SIZE(mod_str))
		csio_info(hw, "Port:%d - %s port module inserted\n", port_id,
			  mod_str[port->mod_type]);
	else if (port->mod_type == FW_PORT_MOD_TYPE_NOTSUPPORTED)
		csio_info(hw,
			  "Port:%d - unsupported optical port module "
			  "inserted\n", port_id);
	else if (port->mod_type == FW_PORT_MOD_TYPE_UNKNOWN)
		csio_info(hw,
			  "Port:%d - unknown port module inserted, forcing "
			  "TWINAX\n", port_id);
	else if (port->mod_type == FW_PORT_MOD_TYPE_ERROR)
		csio_info(hw, "Port:%d - transceiver module error\n", port_id);
	else
		csio_info(hw, "Port:%d - unknown module type %d inserted\n",
			  port_id, port->mod_type);
}

int
csio_mb_fwevt_handler(struct csio_hw *hw, __be64 *cmd)
{
	uint8_t opcode = *(uint8_t *)cmd;
	struct fw_port_cmd *pcmd;
	uint8_t port_id;
	uint32_t link_status;
	uint16_t action;
	uint8_t mod_type;
	fw_port_cap32_t linkattr;

	if (opcode == FW_PORT_CMD) {
		pcmd = (struct fw_port_cmd *)cmd;
		port_id = FW_PORT_CMD_PORTID_G(
				ntohl(pcmd->op_to_portid));
		action = FW_PORT_CMD_ACTION_G(
				ntohl(pcmd->action_to_len16));
		if (action != FW_PORT_ACTION_GET_PORT_INFO &&
		    action != FW_PORT_ACTION_GET_PORT_INFO32) {
			csio_err(hw, "Unhandled FW_PORT_CMD action: %u\n",
				action);
			return -EINVAL;
		}

		if (action == FW_PORT_ACTION_GET_PORT_INFO) {
			link_status = ntohl(pcmd->u.info.lstatus_to_modtype);
			mod_type = FW_PORT_CMD_MODTYPE_G(link_status);
			linkattr = lstatus_to_fwcap(link_status);

			hw->pport[port_id].link_status =
				FW_PORT_CMD_LSTATUS_G(link_status);
		} else {
			link_status =
				ntohl(pcmd->u.info32.lstatus32_to_cbllen32);
			mod_type = FW_PORT_CMD_MODTYPE32_G(link_status);
			linkattr = ntohl(pcmd->u.info32.linkattr32);

			hw->pport[port_id].link_status =
				FW_PORT_CMD_LSTATUS32_G(link_status);
		}

		hw->pport[port_id].link_speed = fwcap_to_fwspeed(linkattr);

		csio_info(hw, "Port:%x - LINK %s\n", port_id,
			hw->pport[port_id].link_status ? "UP" : "DOWN");

		if (mod_type != hw->pport[port_id].mod_type) {
			hw->pport[port_id].mod_type = mod_type;
			csio_mb_portmod_changed(hw, port_id);
		}
	} else if (opcode == FW_DEBUG_CMD) {
		csio_mb_dump_fw_dbg(hw, cmd);
	} else {
		csio_dbg(hw, "Gen MB can't handle op:0x%x on evtq.\n", opcode);
		return -EINVAL;
	}

	return 0;
}

/*
 * csio_mb_isr_handler - Handle mailboxes related interrupts.
 * @hw: The HW structure
 *
 * Called from the ISR to handle Mailbox related interrupts.
 * HW Lock should be held across this call.
 */
int
csio_mb_isr_handler(struct csio_hw *hw)
{
	struct csio_mbm		*mbm = &hw->mbm;
	struct csio_mb		*mbp =  mbm->mcurrent;
	__be64			*cmd;
	uint32_t		ctl, cim_cause, pl_cause;
	int			i;
	uint32_t	ctl_reg = PF_REG(hw->pfn, CIM_PF_MAILBOX_CTRL_A);
	uint32_t	data_reg = PF_REG(hw->pfn, CIM_PF_MAILBOX_DATA_A);
	int			size;
	__be64			hdr;
	struct fw_cmd_hdr	*fw_hdr;

	pl_cause = csio_rd_reg32(hw, MYPF_REG(PL_PF_INT_CAUSE_A));
	cim_cause = csio_rd_reg32(hw, MYPF_REG(CIM_PF_HOST_INT_CAUSE_A));

	if (!(pl_cause & PFCIM_F) || !(cim_cause & MBMSGRDYINT_F)) {
		CSIO_INC_STATS(hw, n_mbint_unexp);
		return -EINVAL;
	}

	/*
	 * The cause registers below HAVE to be cleared in the SAME
	 * order as below: The low level cause register followed by
	 * the upper level cause register. In other words, CIM-cause
	 * first followed by PL-Cause next.
	 */
	csio_wr_reg32(hw, MBMSGRDYINT_F, MYPF_REG(CIM_PF_HOST_INT_CAUSE_A));
	csio_wr_reg32(hw, PFCIM_F, MYPF_REG(PL_PF_INT_CAUSE_A));

	ctl = csio_rd_reg32(hw, ctl_reg);

	if (csio_mb_is_host_owner(MBOWNER_G(ctl))) {

		CSIO_DUMP_MB(hw, hw->pfn, data_reg);

		if (!(ctl & MBMSGVALID_F)) {
			csio_warn(hw,
				  "Stray mailbox interrupt recvd,"
				  " mailbox data not valid\n");
			csio_wr_reg32(hw, 0, ctl_reg);
			/* Flush */
			csio_rd_reg32(hw, ctl_reg);
			return -EINVAL;
		}

		hdr = cpu_to_be64(csio_rd_reg64(hw, data_reg));
		fw_hdr = (struct fw_cmd_hdr *)&hdr;

		switch (FW_CMD_OP_G(ntohl(fw_hdr->hi))) {
		case FW_DEBUG_CMD:
			csio_mb_debug_cmd_handler(hw);
			return -EINVAL;
#if 0
		case FW_ERROR_CMD:
		case FW_INITIALIZE_CMD: /* When we are not master */
#endif
		}

		CSIO_ASSERT(mbp != NULL);

		cmd = mbp->mb;
		size = mbp->mb_size;
		/* Get response */
		for (i = 0; i < size; i += 8)
			*cmd++ = cpu_to_be64(csio_rd_reg64
						  (hw, data_reg + i));

		csio_wr_reg32(hw, 0, ctl_reg);
		/* Flush */
		csio_rd_reg32(hw, ctl_reg);

		mbm->mcurrent = NULL;

		/* Add completion to tail of cbfn queue */
		list_add_tail(&mbp->list, &mbm->cbfn_q);
		CSIO_INC_STATS(mbm, n_cbfnq);

		/*
		 * Enqueue event to EventQ. Events processing happens
		 * in Event worker thread context
		 */
		if (csio_enqueue_evt(hw, CSIO_EVT_MBX, mbp, sizeof(mbp)))
			CSIO_INC_STATS(hw, n_evt_drop);

		return 0;

	} else {
		/*
		 * We can get here if mailbox MSIX vector is shared,
		 * or in INTx case. Or a stray interrupt.
		 */
		csio_dbg(hw, "Host not owner, no mailbox interrupt\n");
		CSIO_INC_STATS(hw, n_int_stray);
		return -EINVAL;
	}
}

/*
 * csio_mb_tmo_handler - Timeout handler
 * @hw: The HW structure
 *
 */
struct csio_mb *
csio_mb_tmo_handler(struct csio_hw *hw)
{
	struct csio_mbm *mbm = &hw->mbm;
	struct csio_mb *mbp =  mbm->mcurrent;
	struct fw_cmd_hdr *fw_hdr;

	/*
	 * Could be a race b/w the completion handler and the timer
	 * and the completion handler won that race.
	 */
	if (mbp == NULL) {
		CSIO_DB_ASSERT(0);
		return NULL;
	}

	fw_hdr = (struct fw_cmd_hdr *)(mbp->mb);

	csio_dbg(hw, "Mailbox num:%x op:0x%x timed out\n", hw->pfn,
		    FW_CMD_OP_G(ntohl(fw_hdr->hi)));

	mbm->mcurrent = NULL;
	CSIO_INC_STATS(mbm, n_tmo);
	fw_hdr->lo = htonl(FW_CMD_RETVAL_V(FW_ETIMEDOUT));

	return mbp;
}

/*
 * csio_mb_cancel_all - Cancel all waiting commands.
 * @hw: The HW structure
 * @cbfn_q: The callback queue.
 *
 * Caller should hold hw lock across this call.
 */
void
csio_mb_cancel_all(struct csio_hw *hw, struct list_head *cbfn_q)
{
	struct csio_mb *mbp;
	struct csio_mbm *mbm = &hw->mbm;
	struct fw_cmd_hdr *hdr;
	struct list_head *tmp;

	if (mbm->mcurrent) {
		mbp = mbm->mcurrent;

		/* Stop mailbox completion timer */
		del_timer_sync(&mbm->timer);

		/* Add completion to tail of cbfn queue */
		list_add_tail(&mbp->list, cbfn_q);
		mbm->mcurrent = NULL;
	}

	if (!list_empty(&mbm->req_q)) {
		list_splice_tail_init(&mbm->req_q, cbfn_q);
		mbm->stats.n_activeq = 0;
	}

	if (!list_empty(&mbm->cbfn_q)) {
		list_splice_tail_init(&mbm->cbfn_q, cbfn_q);
		mbm->stats.n_cbfnq = 0;
	}

	if (list_empty(cbfn_q))
		return;

	list_for_each(tmp, cbfn_q) {
		mbp = (struct csio_mb *)tmp;
		hdr = (struct fw_cmd_hdr *)(mbp->mb);

		csio_dbg(hw, "Cancelling pending mailbox num %x op:%x\n",
			    hw->pfn, FW_CMD_OP_G(ntohl(hdr->hi)));

		CSIO_INC_STATS(mbm, n_cancel);
		hdr->lo = htonl(FW_CMD_RETVAL_V(FW_HOSTERROR));
	}
}

/*
 * csio_mbm_init - Initialize Mailbox module
 * @mbm: Mailbox module
 * @hw: The HW structure
 * @timer: Timing function for interrupting mailboxes
 *
 * Initialize timer and the request/response queues.
 */
int
csio_mbm_init(struct csio_mbm *mbm, struct csio_hw *hw,
	      void (*timer_fn)(struct timer_list *))
{
	mbm->hw = hw;
	timer_setup(&mbm->timer, timer_fn, 0);

	INIT_LIST_HEAD(&mbm->req_q);
	INIT_LIST_HEAD(&mbm->cbfn_q);
	csio_set_mb_intr_idx(mbm, -1);

	return 0;
}

/*
 * csio_mbm_exit - Uninitialize mailbox module
 * @mbm: Mailbox module
 *
 * Stop timer.
 */
void
csio_mbm_exit(struct csio_mbm *mbm)
{
	del_timer_sync(&mbm->timer);

	CSIO_DB_ASSERT(mbm->mcurrent == NULL);
	CSIO_DB_ASSERT(list_empty(&mbm->req_q));
	CSIO_DB_ASSERT(list_empty(&mbm->cbfn_q));
}
