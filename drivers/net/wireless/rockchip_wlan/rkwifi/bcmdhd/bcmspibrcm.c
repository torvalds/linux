/*
 * Broadcom BCMSDH to gSPI Protocol Conversion Layer
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmspibrcm.c 591086 2015-10-07 02:51:01Z $
 */

#define HSMODE

#include <typedefs.h>

#include <bcmdevs.h>
#include <bcmendian.h>
#include <bcmutils.h>
#include <osl.h>
#include <hndsoc.h>
#include <siutils.h>
#include <sbchipc.h>
#include <sbsdio.h>	/* SDIO device core hardware definitions. */
#include <spid.h>

#include <bcmsdbus.h>	/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>	/* ioctl/iovars */
#include <sdio.h>	/* SDIO Device and Protocol Specs */

#include <pcicfg.h>


#include <bcmspibrcm.h>
#include <bcmspi.h>

/* these are for the older cores... for newer cores we have control for each of them */
#define F0_RESPONSE_DELAY	16
#define F1_RESPONSE_DELAY	16
#define F2_RESPONSE_DELAY	F0_RESPONSE_DELAY


#define GSPI_F0_RESP_DELAY		0
#define GSPI_F1_RESP_DELAY		F1_RESPONSE_DELAY
#define GSPI_F2_RESP_DELAY		0
#define GSPI_F3_RESP_DELAY		0

#define CMDLEN		4

#define DWORDMODE_ON (sd->chip == BCM4329_CHIP_ID) && (sd->chiprev == 2) && (sd->dwordmode == TRUE)

/* Globals */
#if defined(DHD_DEBUG)
uint sd_msglevel = SDH_ERROR_VAL;
#else
uint sd_msglevel = 0;
#endif 

uint sd_hiok = FALSE;		/* Use hi-speed mode if available? */
uint sd_sdmode = SDIOH_MODE_SPI;		/* Use SD4 mode by default */
uint sd_f2_blocksize = 64;		/* Default blocksize */


uint sd_divisor = 2;
uint sd_power = 1;		/* Default to SD Slot powered ON */
uint sd_clock = 1;		/* Default to SD Clock turned ON */
uint sd_crc = 0;		/* Default to SPI CRC Check turned OFF */
uint sd_pci_slot = 0xFFFFffff; /* Used to force selection of a particular PCI slot */

uint8	spi_outbuf[SPI_MAX_PKT_LEN];
uint8	spi_inbuf[SPI_MAX_PKT_LEN];

/* 128bytes buffer is enough to clear data-not-available and program response-delay F0 bits
 * assuming we will not exceed F0 response delay > 100 bytes at 48MHz.
 */
#define BUF2_PKT_LEN	128
uint8	spi_outbuf2[BUF2_PKT_LEN];
uint8	spi_inbuf2[BUF2_PKT_LEN];

#define SPISWAP_WD4(x) bcmswap32(x);
#define SPISWAP_WD2(x) (bcmswap16(x & 0xffff)) | \
						(bcmswap16((x & 0xffff0000) >> 16) << 16);

/* Prototypes */
static bool bcmspi_test_card(sdioh_info_t *sd);
static bool bcmspi_host_device_init_adapt(sdioh_info_t *sd);
static int bcmspi_set_highspeed_mode(sdioh_info_t *sd, bool hsmode);
static int bcmspi_cmd_issue(sdioh_info_t *sd, bool use_dma, uint32 cmd_arg,
                           uint32 *data, uint32 datalen);
static int bcmspi_card_regread(sdioh_info_t *sd, int func, uint32 regaddr,
                              int regsize, uint32 *data);
static int bcmspi_card_regwrite(sdioh_info_t *sd, int func, uint32 regaddr,
                               int regsize, uint32 data);
static int bcmspi_card_bytewrite(sdioh_info_t *sd, int func, uint32 regaddr,
                               uint8 *data);
static int bcmspi_driver_init(sdioh_info_t *sd);
static int bcmspi_card_buf(sdioh_info_t *sd, int rw, int func, bool fifo,
                          uint32 addr, int nbytes, uint32 *data);
static int bcmspi_card_regread_fixedaddr(sdioh_info_t *sd, int func, uint32 regaddr, int regsize,
                                 uint32 *data);
static void bcmspi_cmd_getdstatus(sdioh_info_t *sd, uint32 *dstatus_buffer);
static int bcmspi_update_stats(sdioh_info_t *sd, uint32 cmd_arg);

/*
 *  Public entry points & extern's
 */
extern sdioh_info_t *
sdioh_attach(osl_t *osh, void *bar0, uint irq)
{
	sdioh_info_t *sd;

	sd_trace(("%s\n", __FUNCTION__));
	if ((sd = (sdioh_info_t *)MALLOC(osh, sizeof(sdioh_info_t))) == NULL) {
		sd_err(("%s: out of memory, malloced %d bytes\n", __FUNCTION__, MALLOCED(osh)));
		return NULL;
	}
	bzero((char *)sd, sizeof(sdioh_info_t));
	sd->osh = osh;
	if (spi_osinit(sd) != 0) {
		sd_err(("%s: spi_osinit() failed\n", __FUNCTION__));
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
		return NULL;
	}

	sd->bar0 = bar0;
	sd->irq = irq;
	sd->intr_handler = NULL;
	sd->intr_handler_arg = NULL;
	sd->intr_handler_valid = FALSE;

	/* Set defaults */
	sd->use_client_ints = TRUE;
	sd->sd_use_dma = FALSE;	/* DMA Not supported */

	/* Spi device default is 16bit mode, change to 4 when device is changed to 32bit
	 * mode
	 */
	sd->wordlen = 2;


	if (!spi_hw_attach(sd)) {
		sd_err(("%s: spi_hw_attach() failed\n", __FUNCTION__));
		spi_osfree(sd);
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
		return (NULL);
	}

	if (bcmspi_driver_init(sd) != SUCCESS) {
		sd_err(("%s: bcmspi_driver_init() failed()\n", __FUNCTION__));
		spi_hw_detach(sd);
		spi_osfree(sd);
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
		return (NULL);
	}

	if (spi_register_irq(sd, irq) != SUCCESS) {
		sd_err(("%s: spi_register_irq() failed for irq = %d\n", __FUNCTION__, irq));
		spi_hw_detach(sd);
		spi_osfree(sd);
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
		return (NULL);
	}

	sd_trace(("%s: Done\n", __FUNCTION__));

	return sd;
}

extern SDIOH_API_RC
sdioh_detach(osl_t *osh, sdioh_info_t *sd)
{
	sd_trace(("%s\n", __FUNCTION__));
	if (sd) {
		sd_err(("%s: detaching from hardware\n", __FUNCTION__));
		spi_free_irq(sd->irq, sd);
		spi_hw_detach(sd);
		spi_osfree(sd);
		MFREE(sd->osh, sd, sizeof(sdioh_info_t));
	}
	return SDIOH_API_RC_SUCCESS;
}

/* Configure callback to client when we recieve client interrupt */
extern SDIOH_API_RC
sdioh_interrupt_register(sdioh_info_t *sd, sdioh_cb_fn_t fn, void *argh)
{
	sd_trace(("%s: Entering\n", __FUNCTION__));
#if !defined(OOB_INTR_ONLY)
	sd->intr_handler = fn;
	sd->intr_handler_arg = argh;
	sd->intr_handler_valid = TRUE;
#endif /* !defined(OOB_INTR_ONLY) */
	return SDIOH_API_RC_SUCCESS;
}

extern SDIOH_API_RC
sdioh_interrupt_deregister(sdioh_info_t *sd)
{
	sd_trace(("%s: Entering\n", __FUNCTION__));
#if !defined(OOB_INTR_ONLY)
	sd->intr_handler_valid = FALSE;
	sd->intr_handler = NULL;
	sd->intr_handler_arg = NULL;
#endif /* !defined(OOB_INTR_ONLY) */
	return SDIOH_API_RC_SUCCESS;
}

extern SDIOH_API_RC
sdioh_interrupt_query(sdioh_info_t *sd, bool *onoff)
{
	sd_trace(("%s: Entering\n", __FUNCTION__));
	*onoff = sd->client_intr_enabled;
	return SDIOH_API_RC_SUCCESS;
}

#if defined(DHD_DEBUG)
extern bool
sdioh_interrupt_pending(sdioh_info_t *sd)
{
	return 0;
}
#endif

extern SDIOH_API_RC
sdioh_query_device(sdioh_info_t *sd)
{
	/* Return a BRCM ID appropriate to the dongle class */
	return (sd->num_funcs > 1) ? BCM4329_D11N_ID : BCM4318_D11G_ID;
}

/* Provide dstatus bits of spi-transaction for dhd layers. */
extern uint32
sdioh_get_dstatus(sdioh_info_t *sd)
{
	return sd->card_dstatus;
}

extern void
sdioh_chipinfo(sdioh_info_t *sd, uint32 chip, uint32 chiprev)
{
	sd->chip = chip;
	sd->chiprev = chiprev;
}

extern void
sdioh_dwordmode(sdioh_info_t *sd, bool set)
{
	uint8 reg = 0;
	int status;

	if ((status = sdioh_request_byte(sd, SDIOH_READ, SPI_FUNC_0, SPID_STATUS_ENABLE, &reg)) !=
	     SUCCESS) {
		sd_err(("%s: Failed to set dwordmode in gSPI\n", __FUNCTION__));
		return;
	}

	if (set) {
		reg |= DWORD_PKT_LEN_EN;
		sd->dwordmode = TRUE;
		sd->client_block_size[SPI_FUNC_2] = 4096; /* h2spi's limit is 4KB, we support 8KB */
	} else {
		reg &= ~DWORD_PKT_LEN_EN;
		sd->dwordmode = FALSE;
		sd->client_block_size[SPI_FUNC_2] = 2048;
	}

	if ((status = sdioh_request_byte(sd, SDIOH_WRITE, SPI_FUNC_0, SPID_STATUS_ENABLE, &reg)) !=
	     SUCCESS) {
		sd_err(("%s: Failed to set dwordmode in gSPI\n", __FUNCTION__));
		return;
	}
}


uint
sdioh_query_iofnum(sdioh_info_t *sd)
{
	return sd->num_funcs;
}

/* IOVar table */
enum {
	IOV_MSGLEVEL = 1,
	IOV_BLOCKMODE,
	IOV_BLOCKSIZE,
	IOV_DMA,
	IOV_USEINTS,
	IOV_NUMINTS,
	IOV_NUMLOCALINTS,
	IOV_HOSTREG,
	IOV_DEVREG,
	IOV_DIVISOR,
	IOV_SDMODE,
	IOV_HISPEED,
	IOV_HCIREGS,
	IOV_POWER,
	IOV_CLOCK,
	IOV_SPIERRSTATS,
	IOV_RESP_DELAY_ALL
};

const bcm_iovar_t sdioh_iovars[] = {
	{"sd_msglevel",	IOV_MSGLEVEL, 	0,	IOVT_UINT32,	0 },
	{"sd_blocksize", IOV_BLOCKSIZE, 0,	IOVT_UINT32,	0 }, /* ((fn << 16) | size) */
	{"sd_dma",	IOV_DMA,	0,	IOVT_BOOL,	0 },
	{"sd_ints",	IOV_USEINTS,	0,	IOVT_BOOL,	0 },
	{"sd_numints",	IOV_NUMINTS,	0,	IOVT_UINT32,	0 },
	{"sd_numlocalints", IOV_NUMLOCALINTS, 0, IOVT_UINT32,	0 },
	{"sd_hostreg",	IOV_HOSTREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t) },
	{"sd_devreg",	IOV_DEVREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t)	},
	{"sd_divisor",	IOV_DIVISOR,	0,	IOVT_UINT32,	0 },
	{"sd_power",	IOV_POWER,	0,	IOVT_UINT32,	0 },
	{"sd_clock",	IOV_CLOCK,	0,	IOVT_UINT32,	0 },
	{"sd_mode",	IOV_SDMODE,	0,	IOVT_UINT32,	100},
	{"sd_highspeed",	IOV_HISPEED,	0,	IOVT_UINT32,	0},
	{"spi_errstats", IOV_SPIERRSTATS, 0, IOVT_BUFFER, sizeof(struct spierrstats_t) },
	{"spi_respdelay",	IOV_RESP_DELAY_ALL,	0,	IOVT_BOOL,	0 },
	{NULL, 0, 0, 0, 0 }
};

int
sdioh_iovar_op(sdioh_info_t *si, const char *name,
               void *params, int plen, void *arg, int len, bool set)
{
	const bcm_iovar_t *vi = NULL;
	int bcmerror = 0;
	int val_size;
	int32 int_val = 0;
	bool bool_val;
	uint32 actionid;
/*
	sdioh_regs_t *regs;
*/

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get must have return space; Set does not take qualifiers */
	ASSERT(set || (arg && len));
	ASSERT(!set || (!params && !plen));

	sd_trace(("%s: Enter (%s %s)\n", __FUNCTION__, (set ? "set" : "get"), name));

	if ((vi = bcm_iovar_lookup(sdioh_iovars, name)) == NULL) {
		bcmerror = BCME_UNSUPPORTED;
		goto exit;
	}

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, set)) != 0)
		goto exit;

	/* Set up params so get and set can share the convenience variables */
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		val_size = sizeof(int);

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	bool_val = (int_val != 0) ? TRUE : FALSE;

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);
	switch (actionid) {
	case IOV_GVAL(IOV_MSGLEVEL):
		int_val = (int32)sd_msglevel;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_MSGLEVEL):
		sd_msglevel = int_val;
		break;

	case IOV_GVAL(IOV_BLOCKSIZE):
		if ((uint32)int_val > si->num_funcs) {
			bcmerror = BCME_BADARG;
			break;
		}
		int_val = (int32)si->client_block_size[int_val];
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_DMA):
		int_val = (int32)si->sd_use_dma;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DMA):
		si->sd_use_dma = (bool)int_val;
		break;

	case IOV_GVAL(IOV_USEINTS):
		int_val = (int32)si->use_client_ints;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_USEINTS):
		break;

	case IOV_GVAL(IOV_DIVISOR):
		int_val = (uint32)sd_divisor;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DIVISOR):
		sd_divisor = int_val;
		if (!spi_start_clock(si, (uint16)sd_divisor)) {
			sd_err(("%s: set clock failed\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
		}
		break;

	case IOV_GVAL(IOV_POWER):
		int_val = (uint32)sd_power;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_POWER):
		sd_power = int_val;
		break;

	case IOV_GVAL(IOV_CLOCK):
		int_val = (uint32)sd_clock;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_CLOCK):
		sd_clock = int_val;
		break;

	case IOV_GVAL(IOV_SDMODE):
		int_val = (uint32)sd_sdmode;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SDMODE):
		sd_sdmode = int_val;
		break;

	case IOV_GVAL(IOV_HISPEED):
		int_val = (uint32)sd_hiok;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_HISPEED):
		sd_hiok = int_val;

		if (!bcmspi_set_highspeed_mode(si, (bool)sd_hiok)) {
			sd_err(("%s: Failed changing highspeed mode to %d.\n",
			        __FUNCTION__, sd_hiok));
			bcmerror = BCME_ERROR;
			return ERROR;
		}
		break;

	case IOV_GVAL(IOV_NUMINTS):
		int_val = (int32)si->intrcount;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_NUMLOCALINTS):
		int_val = (int32)si->local_intrcount;
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_GVAL(IOV_DEVREG):
	{
		sdreg_t *sd_ptr = (sdreg_t *)params;
		uint8 data;

		if (sdioh_cfg_read(si, sd_ptr->func, sd_ptr->offset, &data)) {
			bcmerror = BCME_SDIO_ERROR;
			break;
		}

		int_val = (int)data;
		bcopy(&int_val, arg, sizeof(int_val));
		break;
	}

	case IOV_SVAL(IOV_DEVREG):
	{
		sdreg_t *sd_ptr = (sdreg_t *)params;
		uint8 data = (uint8)sd_ptr->value;

		if (sdioh_cfg_write(si, sd_ptr->func, sd_ptr->offset, &data)) {
			bcmerror = BCME_SDIO_ERROR;
			break;
		}
		break;
	}


	case IOV_GVAL(IOV_SPIERRSTATS):
	{
		bcopy(&si->spierrstats, arg, sizeof(struct spierrstats_t));
		break;
	}

	case IOV_SVAL(IOV_SPIERRSTATS):
	{
		bzero(&si->spierrstats, sizeof(struct spierrstats_t));
		break;
	}

	case IOV_GVAL(IOV_RESP_DELAY_ALL):
		int_val = (int32)si->resp_delay_all;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RESP_DELAY_ALL):
		si->resp_delay_all = (bool)int_val;
		int_val = STATUS_ENABLE|INTR_WITH_STATUS;
		if (si->resp_delay_all)
			int_val |= RESP_DELAY_ALL;
		else {
			if (bcmspi_card_regwrite(si, SPI_FUNC_0, SPID_RESPONSE_DELAY, 1,
			     F1_RESPONSE_DELAY) != SUCCESS) {
				sd_err(("%s: Unable to set response delay.\n", __FUNCTION__));
				bcmerror = BCME_SDIO_ERROR;
				break;
			}
		}

		if (bcmspi_card_regwrite(si, SPI_FUNC_0, SPID_STATUS_ENABLE, 1, int_val)
		     != SUCCESS) {
			sd_err(("%s: Unable to set response delay.\n", __FUNCTION__));
			bcmerror = BCME_SDIO_ERROR;
			break;
		}
		break;

	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}
exit:

	return bcmerror;
}

extern SDIOH_API_RC
sdioh_cfg_read(sdioh_info_t *sd, uint fnc_num, uint32 addr, uint8 *data)
{
	SDIOH_API_RC status;
	/* No lock needed since sdioh_request_byte does locking */
	status = sdioh_request_byte(sd, SDIOH_READ, fnc_num, addr, data);
	return status;
}

extern SDIOH_API_RC
sdioh_cfg_write(sdioh_info_t *sd, uint fnc_num, uint32 addr, uint8 *data)
{
	/* No lock needed since sdioh_request_byte does locking */
	SDIOH_API_RC status;

	if ((fnc_num == SPI_FUNC_1) && (addr == SBSDIO_FUNC1_FRAMECTRL)) {
		uint8 dummy_data;
		status = sdioh_cfg_read(sd, fnc_num, addr, &dummy_data);
		if (status) {
			sd_err(("sdioh_cfg_read() failed.\n"));
			return status;
		}
	}

	status = sdioh_request_byte(sd, SDIOH_WRITE, fnc_num, addr, data);
	return status;
}

extern SDIOH_API_RC
sdioh_cis_read(sdioh_info_t *sd, uint func, uint8 *cisd, uint32 length)
{
	uint32 count;
	int offset;
	uint32 cis_byte;
	uint16 *cis = (uint16 *)cisd;
	uint bar0 = SI_ENUM_BASE;
	int status;
	uint8 data;

	sd_trace(("%s: Func %d\n", __FUNCTION__, func));

	spi_lock(sd);

	/* Set sb window address to 0x18000000 */
	data = (bar0 >> 8) & SBSDIO_SBADDRLOW_MASK;
	status = bcmspi_card_bytewrite(sd, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW, &data);
	if (status == SUCCESS) {
		data = (bar0 >> 16) & SBSDIO_SBADDRMID_MASK;
		status = bcmspi_card_bytewrite(sd, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRMID, &data);
	} else {
		sd_err(("%s: Unable to set sb-addr-windows\n", __FUNCTION__));
		spi_unlock(sd);
		return (BCME_ERROR);
	}
	if (status == SUCCESS) {
		data = (bar0 >> 24) & SBSDIO_SBADDRHIGH_MASK;
		status = bcmspi_card_bytewrite(sd, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRHIGH, &data);
	} else {
		sd_err(("%s: Unable to set sb-addr-windows\n", __FUNCTION__));
		spi_unlock(sd);
		return (BCME_ERROR);
	}

	offset =  CC_SROM_OTP; /* OTP offset in chipcommon. */
	for (count = 0; count < length/2; count++) {
		if (bcmspi_card_regread (sd, SDIO_FUNC_1, offset, 2, &cis_byte) < 0) {
			sd_err(("%s: regread failed: Can't read CIS\n", __FUNCTION__));
			spi_unlock(sd);
			return (BCME_ERROR);
		}

		*cis = (uint16)cis_byte;
		cis++;
		offset += 2;
	}

	spi_unlock(sd);

	return (BCME_OK);
}

extern SDIOH_API_RC
sdioh_request_byte(sdioh_info_t *sd, uint rw, uint func, uint regaddr, uint8 *byte)
{
	int status;
	uint32 cmd_arg;
	uint32 dstatus;
	uint32 data = (uint32)(*byte);

	spi_lock(sd);

	cmd_arg = 0;
	cmd_arg = SFIELD(cmd_arg, SPI_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, SPI_ACCESS, 1);	/* Incremental access */
	cmd_arg = SFIELD(cmd_arg, SPI_REG_ADDR, regaddr);
	cmd_arg = SFIELD(cmd_arg, SPI_RW_FLAG, rw == SDIOH_READ ? 0 : 1);
	cmd_arg = SFIELD(cmd_arg, SPI_LEN, 1);

	if (rw == SDIOH_READ) {
		sd_trace(("%s: RD cmd_arg=0x%x func=%d regaddr=0x%x\n",
		          __FUNCTION__, cmd_arg, func, regaddr));
	} else {
		sd_trace(("%s: WR cmd_arg=0x%x func=%d regaddr=0x%x data=0x%x\n",
		          __FUNCTION__, cmd_arg, func, regaddr, data));
	}

	if ((status = bcmspi_cmd_issue(sd, sd->sd_use_dma, cmd_arg, &data, 1)) != SUCCESS) {
		spi_unlock(sd);
		return status;
	}

	if (rw == SDIOH_READ) {
		*byte = (uint8)data;
		sd_trace(("%s: RD result=0x%x\n", __FUNCTION__, *byte));
	}

	bcmspi_cmd_getdstatus(sd, &dstatus);
	if (dstatus)
		sd_trace(("dstatus=0x%x\n", dstatus));

	spi_unlock(sd);
	return SDIOH_API_RC_SUCCESS;
}

extern SDIOH_API_RC
sdioh_request_word(sdioh_info_t *sd, uint cmd_type, uint rw, uint func, uint addr,
                   uint32 *word, uint nbytes)
{
	int status;

	spi_lock(sd);

	if (rw == SDIOH_READ)
		status = bcmspi_card_regread(sd, func, addr, nbytes, word);
	else
		status = bcmspi_card_regwrite(sd, func, addr, nbytes, *word);

	spi_unlock(sd);
	return (status == SUCCESS ?  SDIOH_API_RC_SUCCESS : SDIOH_API_RC_FAIL);
}

extern SDIOH_API_RC
sdioh_request_buffer(sdioh_info_t *sd, uint pio_dma, uint fix_inc, uint rw, uint func,
                     uint addr, uint reg_width, uint buflen_u, uint8 *buffer, void *pkt)
{
	int len;
	int buflen = (int)buflen_u;
	bool fifo = (fix_inc == SDIOH_DATA_FIX);

	spi_lock(sd);

	ASSERT(reg_width == 4);
	ASSERT(buflen_u < (1 << 30));
	ASSERT(sd->client_block_size[func]);

	sd_data(("%s: %c len %d r_cnt %d t_cnt %d, pkt @0x%p\n",
	         __FUNCTION__, rw == SDIOH_READ ? 'R' : 'W',
	         buflen_u, sd->r_cnt, sd->t_cnt, pkt));

	/* Break buffer down into blocksize chunks. */
	while (buflen > 0) {
		len = MIN(sd->client_block_size[func], buflen);
		if (bcmspi_card_buf(sd, rw, func, fifo, addr, len, (uint32 *)buffer) != SUCCESS) {
			sd_err(("%s: bcmspi_card_buf %s failed\n",
				__FUNCTION__, rw == SDIOH_READ ? "Read" : "Write"));
			spi_unlock(sd);
			return SDIOH_API_RC_FAIL;
		}
		buffer += len;
		buflen -= len;
		if (!fifo)
			addr += len;
	}
	spi_unlock(sd);
	return SDIOH_API_RC_SUCCESS;
}

/* This function allows write to gspi bus when another rd/wr function is deep down the call stack.
 * Its main aim is to have simpler spi writes rather than recursive writes.
 * e.g. When there is a need to program response delay on the fly after detecting the SPI-func
 * this call will allow to program the response delay.
 */
static int
bcmspi_card_byterewrite(sdioh_info_t *sd, int func, uint32 regaddr, uint8 byte)
{
	uint32 cmd_arg;
	uint32 datalen = 1;
	uint32 hostlen;

	cmd_arg = 0;

	cmd_arg = SFIELD(cmd_arg, SPI_RW_FLAG, 1);
	cmd_arg = SFIELD(cmd_arg, SPI_ACCESS, 1);	/* Incremental access */
	cmd_arg = SFIELD(cmd_arg, SPI_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, SPI_REG_ADDR, regaddr);
	cmd_arg = SFIELD(cmd_arg, SPI_LEN, datalen);

	sd_trace(("%s cmd_arg = 0x%x\n", __FUNCTION__, cmd_arg));


	/* Set up and issue the SPI command.  MSByte goes out on bus first.  Increase datalen
	 * according to the wordlen mode(16/32bit) the device is in.
	 */
	ASSERT(sd->wordlen == 4 || sd->wordlen == 2);
	datalen = ROUNDUP(datalen, sd->wordlen);

	/* Start by copying command in the spi-outbuffer */
	if (sd->wordlen == 4) { /* 32bit spid */
		*(uint32 *)spi_outbuf2 = SPISWAP_WD4(cmd_arg);
		if (datalen & 0x3)
			datalen += (4 - (datalen & 0x3));
	} else if (sd->wordlen == 2) { /* 16bit spid */
		*(uint32 *)spi_outbuf2 = SPISWAP_WD2(cmd_arg);
		if (datalen & 0x1)
			datalen++;
	} else {
		sd_err(("%s: Host is %d bit spid, could not create SPI command.\n",
		        __FUNCTION__, 8 * sd->wordlen));
		return ERROR;
	}

	/* for Write, put the data into the output buffer  */
	if (datalen != 0) {
			if (sd->wordlen == 4) { /* 32bit spid */
				*(uint32 *)&spi_outbuf2[CMDLEN] = SPISWAP_WD4(byte);
			} else if (sd->wordlen == 2) { /* 16bit spid */
				*(uint32 *)&spi_outbuf2[CMDLEN] = SPISWAP_WD2(byte);
			}
	}

	/* +4 for cmd, +4 for dstatus */
	hostlen = datalen + 8;
	hostlen += (4 - (hostlen & 0x3));
	spi_sendrecv(sd, spi_outbuf2, spi_inbuf2, hostlen);

	/* Last 4bytes are dstatus.  Device is configured to return status bits. */
	if (sd->wordlen == 4) { /* 32bit spid */
		sd->card_dstatus = SPISWAP_WD4(*(uint32 *)&spi_inbuf2[datalen + CMDLEN ]);
	} else if (sd->wordlen == 2) { /* 16bit spid */
		sd->card_dstatus = SPISWAP_WD2(*(uint32 *)&spi_inbuf2[datalen + CMDLEN ]);
	} else {
		sd_err(("%s: Host is %d bit machine, could not read SPI dstatus.\n",
		        __FUNCTION__, 8 * sd->wordlen));
		return ERROR;
	}

	if (sd->card_dstatus)
		sd_trace(("dstatus after byte rewrite = 0x%x\n", sd->card_dstatus));

	return (BCME_OK);
}

/* Program the response delay corresponding to the spi function */
static int
bcmspi_prog_resp_delay(sdioh_info_t *sd, int func, uint8 resp_delay)
{
	if (sd->resp_delay_all == FALSE)
		return (BCME_OK);

	if (sd->prev_fun == func)
		return (BCME_OK);

	if (F0_RESPONSE_DELAY == F1_RESPONSE_DELAY)
		return (BCME_OK);

	bcmspi_card_byterewrite(sd, SPI_FUNC_0, SPID_RESPONSE_DELAY, resp_delay);

	/* Remember function for which to avoid reprogramming resp-delay in next iteration */
	sd->prev_fun = func;

	return (BCME_OK);

}

#define GSPI_RESYNC_PATTERN	0x0

/* A resync pattern is a 32bit MOSI line with all zeros. Its a special command in gSPI.
 * It resets the spi-bkplane logic so that all F1 related ping-pong buffer logic is
 * synchronised and all queued resuests are cancelled.
 */
static int
bcmspi_resync_f1(sdioh_info_t *sd)
{
	uint32 cmd_arg = GSPI_RESYNC_PATTERN, data = 0, datalen = 0;


	/* Set up and issue the SPI command.  MSByte goes out on bus first.  Increase datalen
	 * according to the wordlen mode(16/32bit) the device is in.
	 */
	ASSERT(sd->wordlen == 4 || sd->wordlen == 2);
	datalen = ROUNDUP(datalen, sd->wordlen);

	/* Start by copying command in the spi-outbuffer */
	*(uint32 *)spi_outbuf2 = cmd_arg;

	/* for Write, put the data into the output buffer  */
	*(uint32 *)&spi_outbuf2[CMDLEN] = data;

	/* +4 for cmd, +4 for dstatus */
	spi_sendrecv(sd, spi_outbuf2, spi_inbuf2, datalen + 8);

	/* Last 4bytes are dstatus.  Device is configured to return status bits. */
	if (sd->wordlen == 4) { /* 32bit spid */
		sd->card_dstatus = SPISWAP_WD4(*(uint32 *)&spi_inbuf2[datalen + CMDLEN ]);
	} else if (sd->wordlen == 2) { /* 16bit spid */
		sd->card_dstatus = SPISWAP_WD2(*(uint32 *)&spi_inbuf2[datalen + CMDLEN ]);
	} else {
		sd_err(("%s: Host is %d bit machine, could not read SPI dstatus.\n",
		        __FUNCTION__, 8 * sd->wordlen));
		return ERROR;
	}

	if (sd->card_dstatus)
		sd_trace(("dstatus after resync pattern write = 0x%x\n", sd->card_dstatus));

	return (BCME_OK);
}

uint32 dstatus_count = 0;

static int
bcmspi_update_stats(sdioh_info_t *sd, uint32 cmd_arg)
{
	uint32 dstatus = sd->card_dstatus;
	struct spierrstats_t *spierrstats = &sd->spierrstats;
	int err = SUCCESS;

	sd_trace(("cmd = 0x%x, dstatus = 0x%x\n", cmd_arg, dstatus));

	/* Store dstatus of last few gSPI transactions */
	spierrstats->dstatus[dstatus_count % NUM_PREV_TRANSACTIONS] = dstatus;
	spierrstats->spicmd[dstatus_count % NUM_PREV_TRANSACTIONS] = cmd_arg;
	dstatus_count++;

	if (sd->card_init_done == FALSE)
		return err;

	if (dstatus & STATUS_DATA_NOT_AVAILABLE) {
		spierrstats->dna++;
		sd_trace(("Read data not available on F1 addr = 0x%x\n",
		        GFIELD(cmd_arg, SPI_REG_ADDR)));
		/* Clear dna bit */
		bcmspi_card_byterewrite(sd, SPI_FUNC_0, SPID_INTR_REG, DATA_UNAVAILABLE);
	}

	if (dstatus & STATUS_UNDERFLOW) {
		spierrstats->rdunderflow++;
		sd_err(("FIFO underflow happened due to current F2 read command.\n"));
	}

	if (dstatus & STATUS_OVERFLOW) {
		spierrstats->wroverflow++;
		sd_err(("FIFO overflow happened due to current (F1/F2) write command.\n"));
		bcmspi_card_byterewrite(sd, SPI_FUNC_0, SPID_INTR_REG, F1_OVERFLOW);
		bcmspi_resync_f1(sd);
		sd_err(("Recovering from F1 FIFO overflow.\n"));
	}

	if (dstatus & STATUS_F2_INTR) {
		spierrstats->f2interrupt++;
		sd_trace(("Interrupt from F2.  SW should clear corresponding IntStatus bits\n"));
	}

	if (dstatus & STATUS_F3_INTR) {
		spierrstats->f3interrupt++;
		sd_err(("Interrupt from F3.  SW should clear corresponding IntStatus bits\n"));
	}

	if (dstatus & STATUS_HOST_CMD_DATA_ERR) {
		spierrstats->hostcmddataerr++;
		sd_err(("Error in CMD or Host data, detected by CRC/Checksum (optional)\n"));
	}

	if (dstatus & STATUS_F2_PKT_AVAILABLE) {
		spierrstats->f2pktavailable++;
		sd_trace(("Packet is available/ready in F2 TX FIFO\n"));
		sd_trace(("Packet length = %d\n", sd->dwordmode ?
		         ((dstatus & STATUS_F2_PKT_LEN_MASK) >> (STATUS_F2_PKT_LEN_SHIFT - 2)) :
		         ((dstatus & STATUS_F2_PKT_LEN_MASK) >> STATUS_F2_PKT_LEN_SHIFT)));
	}

	if (dstatus & STATUS_F3_PKT_AVAILABLE) {
		spierrstats->f3pktavailable++;
		sd_err(("Packet is available/ready in F3 TX FIFO\n"));
		sd_err(("Packet length = %d\n",
		        (dstatus & STATUS_F3_PKT_LEN_MASK) >> STATUS_F3_PKT_LEN_SHIFT));
	}

	return err;
}

extern int
sdioh_abort(sdioh_info_t *sd, uint func)
{
	return 0;
}

int
sdioh_start(sdioh_info_t *sd, int stage)
{
	return SUCCESS;
}

int
sdioh_stop(sdioh_info_t *sd)
{
	return SUCCESS;
}

int
sdioh_waitlockfree(sdioh_info_t *sd)
{
	return SUCCESS;
}


/*
 * Private/Static work routines
 */
static int
bcmspi_host_init(sdioh_info_t *sd)
{

	/* Default power on mode */
	sd->sd_mode = SDIOH_MODE_SPI;
	sd->polled_mode = TRUE;
	sd->host_init_done = TRUE;
	sd->card_init_done = FALSE;
	sd->adapter_slot = 1;

	return (SUCCESS);
}

static int
get_client_blocksize(sdioh_info_t *sd)
{
	uint32 regdata[2];
	int status;

	/* Find F1/F2/F3 max packet size */
	if ((status = bcmspi_card_regread(sd, 0, SPID_F1_INFO_REG,
	                                 8, regdata)) != SUCCESS) {
		return status;
	}

	sd_trace(("pkt_size regdata[0] = 0x%x, regdata[1] = 0x%x\n",
	        regdata[0], regdata[1]));

	sd->client_block_size[1] = (regdata[0] & F1_MAX_PKT_SIZE) >> 2;
	sd_trace(("Func1 blocksize = %d\n", sd->client_block_size[1]));
	ASSERT(sd->client_block_size[1] == BLOCK_SIZE_F1);

	sd->client_block_size[2] = ((regdata[0] >> 16) & F2_MAX_PKT_SIZE) >> 2;
	sd_trace(("Func2 blocksize = %d\n", sd->client_block_size[2]));
	ASSERT(sd->client_block_size[2] == BLOCK_SIZE_F2);

	sd->client_block_size[3] = (regdata[1] & F3_MAX_PKT_SIZE) >> 2;
	sd_trace(("Func3 blocksize = %d\n", sd->client_block_size[3]));
	ASSERT(sd->client_block_size[3] == BLOCK_SIZE_F3);

	return 0;
}

static int
bcmspi_client_init(sdioh_info_t *sd)
{
	uint32	status_en_reg = 0;
	sd_trace(("%s: Powering up slot %d\n", __FUNCTION__, sd->adapter_slot));

#ifdef HSMODE
	if (!spi_start_clock(sd, (uint16)sd_divisor)) {
		sd_err(("spi_start_clock failed\n"));
		return ERROR;
	}
#else
	/* Start at ~400KHz clock rate for initialization */
	if (!spi_start_clock(sd, 128)) {
		sd_err(("spi_start_clock failed\n"));
		return ERROR;
	}
#endif /* HSMODE */

	if (!bcmspi_host_device_init_adapt(sd)) {
		sd_err(("bcmspi_host_device_init_adapt failed\n"));
		return ERROR;
	}

	if (!bcmspi_test_card(sd)) {
		sd_err(("bcmspi_test_card failed\n"));
		return ERROR;
	}

	sd->num_funcs = SPI_MAX_IOFUNCS;

	get_client_blocksize(sd);

	/* Apply resync pattern cmd with all zeros to reset spi-bkplane F1 logic */
	bcmspi_resync_f1(sd);

	sd->dwordmode = FALSE;

	bcmspi_card_regread(sd, 0, SPID_STATUS_ENABLE, 1, &status_en_reg);

	sd_trace(("%s: Enabling interrupt with dstatus \n", __FUNCTION__));
	status_en_reg |= INTR_WITH_STATUS;

	if (bcmspi_card_regwrite(sd, SPI_FUNC_0, SPID_STATUS_ENABLE, 1,
	    status_en_reg & 0xff) != SUCCESS) {
		sd_err(("%s: Unable to set response delay for all fun's.\n", __FUNCTION__));
		return ERROR;
	}

#ifndef HSMODE
	/* After configuring for High-Speed mode, set the desired clock rate. */
	if (!spi_start_clock(sd, 4)) {
		sd_err(("spi_start_clock failed\n"));
		return ERROR;
	}
#endif /* HSMODE */

	/* check to see if the response delay needs to be programmed properly */
	{
		uint32 f1_respdelay = 0;
		bcmspi_card_regread(sd, 0, SPID_RESP_DELAY_F1, 1, &f1_respdelay);
		if ((f1_respdelay == 0) || (f1_respdelay == 0xFF)) {
			/* older sdiodevice core and has no separte resp delay for each of */
			sd_err(("older corerev < 4 so use the same resp delay for all funcs\n"));
			sd->resp_delay_new = FALSE;
		}
		else {
			/* older sdiodevice core and has no separte resp delay for each of */
			int ret_val;
			sd->resp_delay_new = TRUE;
			sd_err(("new corerev >= 4 so set the resp delay for each of the funcs\n"));
			sd_trace(("resp delay for funcs f0(%d), f1(%d), f2(%d), f3(%d)\n",
				GSPI_F0_RESP_DELAY, GSPI_F1_RESP_DELAY,
				GSPI_F2_RESP_DELAY, GSPI_F3_RESP_DELAY));
			ret_val = bcmspi_card_regwrite(sd, SPI_FUNC_0, SPID_RESP_DELAY_F0, 1,
				GSPI_F0_RESP_DELAY);
			if (ret_val != SUCCESS) {
				sd_err(("%s: Unable to set response delay for F0\n", __FUNCTION__));
				return ERROR;
			}
			ret_val = bcmspi_card_regwrite(sd, SPI_FUNC_0, SPID_RESP_DELAY_F1, 1,
				GSPI_F1_RESP_DELAY);
			if (ret_val != SUCCESS) {
				sd_err(("%s: Unable to set response delay for F1\n", __FUNCTION__));
				return ERROR;
			}
			ret_val = bcmspi_card_regwrite(sd, SPI_FUNC_0, SPID_RESP_DELAY_F2, 1,
				GSPI_F2_RESP_DELAY);
			if (ret_val != SUCCESS) {
				sd_err(("%s: Unable to set response delay for F2\n", __FUNCTION__));
				return ERROR;
			}
			ret_val = bcmspi_card_regwrite(sd, SPI_FUNC_0, SPID_RESP_DELAY_F3, 1,
				GSPI_F3_RESP_DELAY);
			if (ret_val != SUCCESS) {
				sd_err(("%s: Unable to set response delay for F2\n", __FUNCTION__));
				return ERROR;
			}
		}
	}


	sd->card_init_done = TRUE;

	/* get the device rev to program the prop respdelays */

	return SUCCESS;
}

static int
bcmspi_set_highspeed_mode(sdioh_info_t *sd, bool hsmode)
{
	uint32 regdata;
	int status;

	if ((status = bcmspi_card_regread(sd, 0, SPID_CONFIG,
	                                 4, &regdata)) != SUCCESS)
		return status;

	sd_trace(("In %s spih-ctrl = 0x%x \n", __FUNCTION__, regdata));


	if (hsmode == TRUE) {
		sd_trace(("Attempting to enable High-Speed mode.\n"));

		if (regdata & HIGH_SPEED_MODE) {
			sd_trace(("Device is already in High-Speed mode.\n"));
			return status;
		} else {
			regdata |= HIGH_SPEED_MODE;
			sd_trace(("Writing %08x to device at %08x\n", regdata, SPID_CONFIG));
			if ((status = bcmspi_card_regwrite(sd, 0, SPID_CONFIG,
			                                  4, regdata)) != SUCCESS) {
				return status;
			}
		}
	} else {
		sd_trace(("Attempting to disable High-Speed mode.\n"));

		if (regdata & HIGH_SPEED_MODE) {
			regdata &= ~HIGH_SPEED_MODE;
			sd_trace(("Writing %08x to device at %08x\n", regdata, SPID_CONFIG));
			if ((status = bcmspi_card_regwrite(sd, 0, SPID_CONFIG,
			                                  4, regdata)) != SUCCESS)
				return status;
		}
		 else {
			sd_trace(("Device is already in Low-Speed mode.\n"));
			return status;
		}
	}
	spi_controller_highspeed_mode(sd, hsmode);

	return TRUE;
}

#define bcmspi_find_curr_mode(sd) { \
	sd->wordlen = 2; \
	status = bcmspi_card_regread_fixedaddr(sd, 0, SPID_TEST_READ, 4, &regdata); \
	regdata &= 0xff; \
	if ((regdata == 0xad) || (regdata == 0x5b) || \
	    (regdata == 0x5d) || (regdata == 0x5a)) \
		break; \
	sd->wordlen = 4; \
	status = bcmspi_card_regread_fixedaddr(sd, 0, SPID_TEST_READ, 4, &regdata); \
	regdata &= 0xff; \
	if ((regdata == 0xad) || (regdata == 0x5b) || \
	    (regdata == 0x5d) || (regdata == 0x5a)) \
		break; \
	sd_trace(("Silicon testability issue: regdata = 0x%x." \
		" Expected 0xad, 0x5a, 0x5b or 0x5d.\n", regdata)); \
	OSL_DELAY(100000); \
}

#define INIT_ADAPT_LOOP		100

/* Adapt clock-phase-speed-bitwidth between host and device */
static bool
bcmspi_host_device_init_adapt(sdioh_info_t *sd)
{
	uint32 wrregdata, regdata = 0;
	int status;
	int i;

	/* Due to a silicon testability issue, the first command from the Host
	 * to the device will get corrupted (first bit will be lost). So the
	 * Host should poll the device with a safe read request. ie: The Host
	 * should try to read F0 addr 0x14 using the Fixed address mode
	 * (This will prevent a unintended write command to be detected by device)
	 */
	for (i = 0; i < INIT_ADAPT_LOOP; i++) {
	/* If device was not power-cycled it will stay in 32bit mode with
	 * response-delay-all bit set.  Alternate the iteration so that
	 * read either with or without response-delay for F0 to succeed.
	 */
		bcmspi_find_curr_mode(sd);
		sd->resp_delay_all = (i & 0x1) ? TRUE : FALSE;

		bcmspi_find_curr_mode(sd);
		sd->dwordmode = TRUE;

		bcmspi_find_curr_mode(sd);
		sd->dwordmode = FALSE;
	}

	/* Bail out, device not detected */
	if (i == INIT_ADAPT_LOOP)
		return FALSE;

	/* Softreset the spid logic */
	if ((sd->dwordmode) || (sd->wordlen == 4)) {
		bcmspi_card_regwrite(sd, 0, SPID_RESET_BP, 1, RESET_ON_WLAN_BP_RESET|RESET_SPI);
		bcmspi_card_regread(sd, 0, SPID_RESET_BP, 1, &regdata);
		sd_trace(("reset reg read = 0x%x\n", regdata));
		sd_trace(("dwordmode = %d, wordlen = %d, resp_delay_all = %d\n", sd->dwordmode,
		       sd->wordlen, sd->resp_delay_all));
		/* Restore default state after softreset */
		sd->wordlen = 2;
		sd->dwordmode = FALSE;
	}

	if (sd->wordlen == 4) {
		if ((status = bcmspi_card_regread(sd, 0, SPID_TEST_READ, 4, &regdata)) !=
		     SUCCESS)
				return FALSE;
		if (regdata == TEST_RO_DATA_32BIT_LE) {
			sd_trace(("Spid is already in 32bit LE mode. Value read = 0x%x\n",
			          regdata));
			sd_trace(("Spid power was left on.\n"));
		} else {
			sd_err(("Spid power was left on but signature read failed."
			        " Value read = 0x%x\n", regdata));
			return FALSE;
		}
	} else {
		sd->wordlen = 2;

#define CTRL_REG_DEFAULT	0x00010430 /* according to the host m/c */

		wrregdata = (CTRL_REG_DEFAULT);

		if ((status = bcmspi_card_regread(sd, 0, SPID_TEST_READ, 4, &regdata)) != SUCCESS)
			return FALSE;
		sd_trace(("(we are still in 16bit mode) 32bit READ LE regdata = 0x%x\n", regdata));

#ifndef HSMODE
		wrregdata |= (CLOCK_PHASE | CLOCK_POLARITY);
		wrregdata &= ~HIGH_SPEED_MODE;
		bcmspi_card_regwrite(sd, 0, SPID_CONFIG, 4, wrregdata);
#endif /* HSMODE */

		for (i = 0; i < INIT_ADAPT_LOOP; i++) {
			if ((regdata == 0xfdda7d5b) || (regdata == 0xfdda7d5a)) {
				sd_trace(("0xfeedbead was leftshifted by 1-bit.\n"));
				if ((status = bcmspi_card_regread(sd, 0, SPID_TEST_READ, 4,
				     &regdata)) != SUCCESS)
					return FALSE;
			}
			OSL_DELAY(1000);
		}

#if defined(CHANGE_SPI_INTR_POLARITY_ACTIVE_HIGH)
		/* Change to host controller intr-polarity of active-high */
		wrregdata |= INTR_POLARITY;
#else
		/* Change to host controller intr-polarity of active-low */
		wrregdata &= ~INTR_POLARITY;
#endif /* CHANGE_SPI_INTR_POLARITY_ACTIVE_HIGH */

		sd_trace(("(we are still in 16bit mode) 32bit Write LE reg-ctrl-data = 0x%x\n",
		        wrregdata));
		/* Change to 32bit mode */
		wrregdata |= WORD_LENGTH_32;
		bcmspi_card_regwrite(sd, 0, SPID_CONFIG, 4, wrregdata);

		/* Change command/data packaging in 32bit LE mode */
		sd->wordlen = 4;

		if ((status = bcmspi_card_regread(sd, 0, SPID_TEST_READ, 4, &regdata)) != SUCCESS)
			return FALSE;

		if (regdata == TEST_RO_DATA_32BIT_LE) {
			sd_trace(("Read spid passed. Value read = 0x%x\n", regdata));
			sd_trace(("Spid had power-on cycle OR spi was soft-resetted \n"));
		} else {
			sd_err(("Stale spid reg values read as it was kept powered. Value read ="
			  "0x%x\n", regdata));
			return FALSE;
		}
	}


	return TRUE;
}

static bool
bcmspi_test_card(sdioh_info_t *sd)
{
	uint32 regdata;
	int status;

	if ((status = bcmspi_card_regread(sd, 0, SPID_TEST_READ, 4, &regdata)) != SUCCESS)
		return FALSE;

	if (regdata == (TEST_RO_DATA_32BIT_LE))
		sd_trace(("32bit LE regdata = 0x%x\n", regdata));
	else {
		sd_trace(("Incorrect 32bit LE regdata = 0x%x\n", regdata));
		return FALSE;
	}


#define RW_PATTERN1	0xA0A1A2A3
#define RW_PATTERN2	0x4B5B6B7B

	regdata = RW_PATTERN1;
	if ((status = bcmspi_card_regwrite(sd, 0, SPID_TEST_RW, 4, regdata)) != SUCCESS)
		return FALSE;
	regdata = 0;
	if ((status = bcmspi_card_regread(sd, 0, SPID_TEST_RW, 4, &regdata)) != SUCCESS)
		return FALSE;
	if (regdata != RW_PATTERN1) {
		sd_err(("Write-Read spid failed. Value wrote = 0x%x, Value read = 0x%x\n",
			RW_PATTERN1, regdata));
		return FALSE;
	} else
		sd_trace(("R/W spid passed. Value read = 0x%x\n", regdata));

	regdata = RW_PATTERN2;
	if ((status = bcmspi_card_regwrite(sd, 0, SPID_TEST_RW, 4, regdata)) != SUCCESS)
		return FALSE;
	regdata = 0;
	if ((status = bcmspi_card_regread(sd, 0, SPID_TEST_RW, 4, &regdata)) != SUCCESS)
		return FALSE;
	if (regdata != RW_PATTERN2) {
		sd_err(("Write-Read spid failed. Value wrote = 0x%x, Value read = 0x%x\n",
			RW_PATTERN2, regdata));
		return FALSE;
	} else
		sd_trace(("R/W spid passed. Value read = 0x%x\n", regdata));

	return TRUE;
}

static int
bcmspi_driver_init(sdioh_info_t *sd)
{
	sd_trace(("%s\n", __FUNCTION__));
	if ((bcmspi_host_init(sd)) != SUCCESS) {
		return ERROR;
	}

	if (bcmspi_client_init(sd) != SUCCESS) {
		return ERROR;
	}

	return SUCCESS;
}

/* Read device reg */
static int
bcmspi_card_regread(sdioh_info_t *sd, int func, uint32 regaddr, int regsize, uint32 *data)
{
	int status;
	uint32 cmd_arg, dstatus;

	ASSERT(regsize);

	if (func == 2)
		sd_trace(("Reg access on F2 will generate error indication in dstatus bits.\n"));

	cmd_arg = 0;
	cmd_arg = SFIELD(cmd_arg, SPI_RW_FLAG, 0);
	cmd_arg = SFIELD(cmd_arg, SPI_ACCESS, 1);	/* Incremental access */
	cmd_arg = SFIELD(cmd_arg, SPI_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, SPI_REG_ADDR, regaddr);
	cmd_arg = SFIELD(cmd_arg, SPI_LEN, regsize == BLOCK_SIZE_F2 ? 0 : regsize);

	sd_trace(("%s: RD cmd_arg=0x%x func=%d regaddr=0x%x regsize=%d\n",
	          __FUNCTION__, cmd_arg, func, regaddr, regsize));

	if ((status = bcmspi_cmd_issue(sd, sd->sd_use_dma, cmd_arg, data, regsize)) != SUCCESS)
		return status;

	bcmspi_cmd_getdstatus(sd, &dstatus);
	if (dstatus)
		sd_trace(("dstatus =0x%x\n", dstatus));

	return SUCCESS;
}

static int
bcmspi_card_regread_fixedaddr(sdioh_info_t *sd, int func, uint32 regaddr, int regsize, uint32 *data)
{

	int status;
	uint32 cmd_arg;
	uint32 dstatus;

	ASSERT(regsize);

	if (func == 2)
		sd_trace(("Reg access on F2 will generate error indication in dstatus bits.\n"));

	cmd_arg = 0;
	cmd_arg = SFIELD(cmd_arg, SPI_RW_FLAG, 0);
	cmd_arg = SFIELD(cmd_arg, SPI_ACCESS, 0);	/* Fixed access */
	cmd_arg = SFIELD(cmd_arg, SPI_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, SPI_REG_ADDR, regaddr);
	cmd_arg = SFIELD(cmd_arg, SPI_LEN, regsize);

	sd_trace(("%s: RD cmd_arg=0x%x func=%d regaddr=0x%x regsize=%d\n",
	          __FUNCTION__, cmd_arg, func, regaddr, regsize));

	if ((status = bcmspi_cmd_issue(sd, sd->sd_use_dma, cmd_arg, data, regsize)) != SUCCESS)
		return status;

	sd_trace(("%s: RD result=0x%x\n", __FUNCTION__, *data));

	bcmspi_cmd_getdstatus(sd, &dstatus);
	sd_trace(("dstatus =0x%x\n", dstatus));
	return SUCCESS;
}

/* write a device register */
static int
bcmspi_card_regwrite(sdioh_info_t *sd, int func, uint32 regaddr, int regsize, uint32 data)
{
	int status;
	uint32 cmd_arg, dstatus;

	ASSERT(regsize);

	cmd_arg = 0;

	cmd_arg = SFIELD(cmd_arg, SPI_RW_FLAG, 1);
	cmd_arg = SFIELD(cmd_arg, SPI_ACCESS, 1);	/* Incremental access */
	cmd_arg = SFIELD(cmd_arg, SPI_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, SPI_REG_ADDR, regaddr);
	cmd_arg = SFIELD(cmd_arg, SPI_LEN, regsize == BLOCK_SIZE_F2 ? 0 : regsize);

	sd_trace(("%s: WR cmd_arg=0x%x func=%d regaddr=0x%x regsize=%d data=0x%x\n",
	          __FUNCTION__, cmd_arg, func, regaddr, regsize, data));

	if ((status = bcmspi_cmd_issue(sd, sd->sd_use_dma, cmd_arg, &data, regsize)) != SUCCESS)
		return status;

	bcmspi_cmd_getdstatus(sd, &dstatus);
	if (dstatus)
		sd_trace(("dstatus=0x%x\n", dstatus));

	return SUCCESS;
}

/* write a device register - 1 byte */
static int
bcmspi_card_bytewrite(sdioh_info_t *sd, int func, uint32 regaddr, uint8 *byte)
{
	int status;
	uint32 cmd_arg;
	uint32 dstatus;
	uint32 data = (uint32)(*byte);

	cmd_arg = 0;
	cmd_arg = SFIELD(cmd_arg, SPI_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, SPI_ACCESS, 1);	/* Incremental access */
	cmd_arg = SFIELD(cmd_arg, SPI_REG_ADDR, regaddr);
	cmd_arg = SFIELD(cmd_arg, SPI_RW_FLAG, 1);
	cmd_arg = SFIELD(cmd_arg, SPI_LEN, 1);

	sd_trace(("%s: WR cmd_arg=0x%x func=%d regaddr=0x%x data=0x%x\n",
	          __FUNCTION__, cmd_arg, func, regaddr, data));

	if ((status = bcmspi_cmd_issue(sd, sd->sd_use_dma, cmd_arg, &data, 1)) != SUCCESS)
		return status;

	bcmspi_cmd_getdstatus(sd, &dstatus);
	if (dstatus)
		sd_trace(("dstatus =0x%x\n", dstatus));

	return SUCCESS;
}

void
bcmspi_cmd_getdstatus(sdioh_info_t *sd, uint32 *dstatus_buffer)
{
	*dstatus_buffer = sd->card_dstatus;
}

/* 'data' is of type uint32 whereas other buffers are of type uint8 */
static int
bcmspi_cmd_issue(sdioh_info_t *sd, bool use_dma, uint32 cmd_arg,
                uint32 *data, uint32 datalen)
{
	uint32	i, j;
	uint8	resp_delay = 0;
	int	err = SUCCESS;
	uint32	hostlen;
	uint32 spilen = 0;
	uint32 dstatus_idx = 0;
	uint16 templen, buslen, len, *ptr = NULL;

	sd_trace(("spi cmd = 0x%x\n", cmd_arg));

	if (DWORDMODE_ON) {
		spilen = GFIELD(cmd_arg, SPI_LEN);
		if ((GFIELD(cmd_arg, SPI_FUNCTION) == SPI_FUNC_0) ||
		    (GFIELD(cmd_arg, SPI_FUNCTION) == SPI_FUNC_1))
			dstatus_idx = spilen * 3;

		if ((GFIELD(cmd_arg, SPI_FUNCTION) == SPI_FUNC_2) &&
		    (GFIELD(cmd_arg, SPI_RW_FLAG) == 1)) {
			spilen = spilen << 2;
			dstatus_idx = (spilen % 16) ? (16 - (spilen % 16)) : 0;
			/* convert len to mod16 size */
			spilen = ROUNDUP(spilen, 16);
			cmd_arg = SFIELD(cmd_arg, SPI_LEN, (spilen >> 2));
		}
	}

	/* Set up and issue the SPI command.  MSByte goes out on bus first.  Increase datalen
	 * according to the wordlen mode(16/32bit) the device is in.
	 */
	if (sd->wordlen == 4) { /* 32bit spid */
		*(uint32 *)spi_outbuf = SPISWAP_WD4(cmd_arg);
		if (datalen & 0x3)
			datalen += (4 - (datalen & 0x3));
	} else if (sd->wordlen == 2) { /* 16bit spid */
		*(uint32 *)spi_outbuf = SPISWAP_WD2(cmd_arg);
		if (datalen & 0x1)
			datalen++;
		if (datalen < 4)
			datalen = ROUNDUP(datalen, 4);
	} else {
		sd_err(("Host is %d bit spid, could not create SPI command.\n",
			8 * sd->wordlen));
		return ERROR;
	}

	/* for Write, put the data into the output buffer */
	if (GFIELD(cmd_arg, SPI_RW_FLAG) == 1) {
		/* We send len field of hw-header always a mod16 size, both from host and dongle */
		if (DWORDMODE_ON) {
			if (GFIELD(cmd_arg, SPI_FUNCTION) == SPI_FUNC_2) {
				ptr = (uint16 *)&data[0];
				templen = *ptr;
				/* ASSERT(*ptr == ~*(ptr + 1)); */
				templen = ROUNDUP(templen, 16);
				*ptr = templen;
				sd_trace(("actual tx len = %d\n", (uint16)(~*(ptr+1))));
			}
		}

		if (datalen != 0) {
			for (i = 0; i < datalen/4; i++) {
				if (sd->wordlen == 4) { /* 32bit spid */
					*(uint32 *)&spi_outbuf[i * 4 + CMDLEN] =
						SPISWAP_WD4(data[i]);
				} else if (sd->wordlen == 2) { /* 16bit spid */
					*(uint32 *)&spi_outbuf[i * 4 + CMDLEN] =
						SPISWAP_WD2(data[i]);
				}
			}
		}
	}

	/* Append resp-delay number of bytes and clock them out for F0/1/2 reads. */
	if ((GFIELD(cmd_arg, SPI_RW_FLAG) == 0)) {
		int func = GFIELD(cmd_arg, SPI_FUNCTION);
		switch (func) {
			case 0:
				if (sd->resp_delay_new)
					resp_delay = GSPI_F0_RESP_DELAY;
				else
					resp_delay = sd->resp_delay_all ? F0_RESPONSE_DELAY : 0;
				break;
			case 1:
				if (sd->resp_delay_new)
					resp_delay = GSPI_F1_RESP_DELAY;
				else
					resp_delay = F1_RESPONSE_DELAY;
				break;
			case 2:
				if (sd->resp_delay_new)
					resp_delay = GSPI_F2_RESP_DELAY;
				else
					resp_delay = sd->resp_delay_all ? F2_RESPONSE_DELAY : 0;
				break;
			default:
				ASSERT(0);
				break;
		}
		/* Program response delay */
		if (sd->resp_delay_new == FALSE)
			bcmspi_prog_resp_delay(sd, func, resp_delay);
	}

	/* +4 for cmd and +4 for dstatus */
	hostlen = datalen + 8 + resp_delay;
	hostlen += dstatus_idx;
	hostlen += (4 - (hostlen & 0x3));
	spi_sendrecv(sd, spi_outbuf, spi_inbuf, hostlen);

	/* for Read, get the data into the input buffer */
	if (datalen != 0) {
		if (GFIELD(cmd_arg, SPI_RW_FLAG) == 0) { /* if read cmd */
			for (j = 0; j < datalen/4; j++) {
				if (sd->wordlen == 4) { /* 32bit spid */
					data[j] = SPISWAP_WD4(*(uint32 *)&spi_inbuf[j * 4 +
					            CMDLEN + resp_delay]);
				} else if (sd->wordlen == 2) { /* 16bit spid */
					data[j] = SPISWAP_WD2(*(uint32 *)&spi_inbuf[j * 4 +
					            CMDLEN + resp_delay]);
				}
			}

			if ((DWORDMODE_ON) && (GFIELD(cmd_arg, SPI_FUNCTION) == SPI_FUNC_2)) {
				ptr = (uint16 *)&data[0];
				templen = *ptr;
				buslen = len = ~(*(ptr + 1));
				buslen = ROUNDUP(buslen, 16);
				/* populate actual len in hw-header */
				if (templen == buslen)
					*ptr = len;
			}
		}
	}

	/* Restore back the len field of the hw header */
	if (DWORDMODE_ON) {
		if ((GFIELD(cmd_arg, SPI_FUNCTION) == SPI_FUNC_2) &&
		    (GFIELD(cmd_arg, SPI_RW_FLAG) == 1)) {
			ptr = (uint16 *)&data[0];
			*ptr = (uint16)(~*(ptr+1));
		}
	}

	dstatus_idx += (datalen + CMDLEN + resp_delay);
	/* Last 4bytes are dstatus.  Device is configured to return status bits. */
	if (sd->wordlen == 4) { /* 32bit spid */
		sd->card_dstatus = SPISWAP_WD4(*(uint32 *)&spi_inbuf[dstatus_idx]);
	} else if (sd->wordlen == 2) { /* 16bit spid */
		sd->card_dstatus = SPISWAP_WD2(*(uint32 *)&spi_inbuf[dstatus_idx]);
	} else {
		sd_err(("Host is %d bit machine, could not read SPI dstatus.\n",
			8 * sd->wordlen));
		return ERROR;
	}
	if (sd->card_dstatus == 0xffffffff) {
		sd_err(("looks like not a GSPI device or device is not powered.\n"));
	}

	err = bcmspi_update_stats(sd, cmd_arg);

	return err;

}

static int
bcmspi_card_buf(sdioh_info_t *sd, int rw, int func, bool fifo,
                uint32 addr, int nbytes, uint32 *data)
{
	int status;
	uint32 cmd_arg;
	bool write = rw == SDIOH_READ ? 0 : 1;
	uint retries = 0;

	bool enable;
	uint32	spilen;

	cmd_arg = 0;

	ASSERT(nbytes);
	ASSERT(nbytes <= sd->client_block_size[func]);

	if (write) sd->t_cnt++; else sd->r_cnt++;

	if (func == 2) {
		/* Frame len check limited by gSPI. */
		if ((nbytes > 2000) && write) {
			sd_trace((">2KB write: F2 wr of %d bytes\n", nbytes));
		}
		/* ASSERT(nbytes <= 2048); Fix bigger len gspi issue and uncomment. */
		/* If F2 fifo on device is not ready to receive data, don't do F2 transfer */
		if (write) {
			uint32 dstatus;
			/* check F2 ready with cached one */
			bcmspi_cmd_getdstatus(sd, &dstatus);
			if ((dstatus & STATUS_F2_RX_READY) == 0) {
				retries = WAIT_F2RXFIFORDY;
				enable = 0;
				while (retries-- && !enable) {
					OSL_DELAY(WAIT_F2RXFIFORDY_DELAY * 1000);
					bcmspi_card_regread(sd, SPI_FUNC_0, SPID_STATUS_REG, 4,
					                   &dstatus);
					if (dstatus & STATUS_F2_RX_READY)
						enable = TRUE;
				}
				if (!enable) {
					struct spierrstats_t *spierrstats = &sd->spierrstats;
					spierrstats->f2rxnotready++;
					sd_err(("F2 FIFO is not ready to receive data.\n"));
					return ERROR;
				}
				sd_trace(("No of retries on F2 ready %d\n",
					(WAIT_F2RXFIFORDY - retries)));
			}
		}
	}

	/* F2 transfers happen on 0 addr */
	addr = (func == 2) ? 0 : addr;

	/* In pio mode buffer is read using fixed address fifo in func 1 */
	if ((func == 1) && (fifo))
		cmd_arg = SFIELD(cmd_arg, SPI_ACCESS, 0);
	else
		cmd_arg = SFIELD(cmd_arg, SPI_ACCESS, 1);

	cmd_arg = SFIELD(cmd_arg, SPI_FUNCTION, func);
	cmd_arg = SFIELD(cmd_arg, SPI_REG_ADDR, addr);
	cmd_arg = SFIELD(cmd_arg, SPI_RW_FLAG, write);
	spilen = sd->data_xfer_count = MIN(sd->client_block_size[func], nbytes);
	if ((sd->dwordmode == TRUE) && (GFIELD(cmd_arg, SPI_FUNCTION) == SPI_FUNC_2)) {
		/* convert len to mod4 size */
		spilen = spilen + ((spilen & 0x3) ? (4 - (spilen & 0x3)): 0);
		cmd_arg = SFIELD(cmd_arg, SPI_LEN, (spilen >> 2));
	} else
		cmd_arg = SFIELD(cmd_arg, SPI_LEN, spilen);

	if ((func == 2) && (fifo == 1)) {
		sd_data(("%s: %s func %d, %s, addr 0x%x, len %d bytes, r_cnt %d t_cnt %d\n",
		          __FUNCTION__, write ? "Wr" : "Rd", func, "INCR",
		          addr, nbytes, sd->r_cnt, sd->t_cnt));
	}

	sd_trace(("%s cmd_arg = 0x%x\n", __FUNCTION__, cmd_arg));
	sd_data(("%s: %s func %d, %s, addr 0x%x, len %d bytes, r_cnt %d t_cnt %d\n",
	         __FUNCTION__, write ? "Wd" : "Rd", func, "INCR",
	         addr, nbytes, sd->r_cnt, sd->t_cnt));


	if ((status = bcmspi_cmd_issue(sd, sd->sd_use_dma, cmd_arg, data, nbytes)) != SUCCESS) {
		sd_err(("%s: cmd_issue failed for %s\n", __FUNCTION__,
			(write ? "write" : "read")));
		return status;
	}

	/* gSPI expects that hw-header-len is equal to spi-command-len */
	if ((func == 2) && (rw == SDIOH_WRITE) && (sd->dwordmode == FALSE)) {
		ASSERT((uint16)sd->data_xfer_count == (uint16)(*data & 0xffff));
		ASSERT((uint16)sd->data_xfer_count == (uint16)(~((*data & 0xffff0000) >> 16)));
	}

	if ((nbytes > 2000) && !write) {
		sd_trace((">2KB read: F2 rd of %d bytes\n", nbytes));
	}

	return SUCCESS;
}

/* Reset and re-initialize the device */
int
sdioh_sdio_reset(sdioh_info_t *si)
{
	si->card_init_done = FALSE;
	return bcmspi_client_init(si);
}

SDIOH_API_RC
sdioh_gpioouten(sdioh_info_t *sd, uint32 gpio)
{
	return SDIOH_API_RC_FAIL;
}

SDIOH_API_RC
sdioh_gpioout(sdioh_info_t *sd, uint32 gpio, bool enab)
{
	return SDIOH_API_RC_FAIL;
}

bool
sdioh_gpioin(sdioh_info_t *sd, uint32 gpio)
{
	return FALSE;
}

SDIOH_API_RC
sdioh_gpio_init(sdioh_info_t *sd)
{
	return SDIOH_API_RC_FAIL;
}
