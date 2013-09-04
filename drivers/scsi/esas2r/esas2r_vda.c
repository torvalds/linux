/*
 *  linux/drivers/scsi/esas2r/esas2r_vda.c
 *      esas2r driver VDA firmware interface functions
 *
 *  Copyright (c) 2001-2013 ATTO Technology, Inc.
 *  (mailto:linuxdrivers@attotech.com)
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  NO WARRANTY
 *  THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 *  CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 *  LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 *  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 *  solely responsible for determining the appropriateness of using and
 *  distributing the Program and assumes all risks associated with its
 *  exercise of rights under this Agreement, including but not limited to
 *  the risks and costs of program errors, damage to or loss of data,
 *  programs or equipment, and unavailability or interruption of operations.
 *
 *  DISCLAIMER OF LIABILITY
 *  NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 *  HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include "esas2r.h"

static u8 esas2r_vdaioctl_versions[] = {
	ATTO_VDA_VER_UNSUPPORTED,
	ATTO_VDA_FLASH_VER,
	ATTO_VDA_VER_UNSUPPORTED,
	ATTO_VDA_VER_UNSUPPORTED,
	ATTO_VDA_CLI_VER,
	ATTO_VDA_VER_UNSUPPORTED,
	ATTO_VDA_CFG_VER,
	ATTO_VDA_MGT_VER,
	ATTO_VDA_GSV_VER
};

static void clear_vda_request(struct esas2r_request *rq);

static void esas2r_complete_vda_ioctl(struct esas2r_adapter *a,
				      struct esas2r_request *rq);

/* Prepare a VDA IOCTL request to be sent to the firmware. */
bool esas2r_process_vda_ioctl(struct esas2r_adapter *a,
			      struct atto_ioctl_vda *vi,
			      struct esas2r_request *rq,
			      struct esas2r_sg_context *sgc)
{
	u32 datalen = 0;
	struct atto_vda_sge *firstsg = NULL;
	u8 vercnt = (u8)ARRAY_SIZE(esas2r_vdaioctl_versions);

	vi->status = ATTO_STS_SUCCESS;
	vi->vda_status = RS_PENDING;

	if (vi->function >= vercnt) {
		vi->status = ATTO_STS_INV_FUNC;
		return false;
	}

	if (vi->version > esas2r_vdaioctl_versions[vi->function]) {
		vi->status = ATTO_STS_INV_VERSION;
		return false;
	}

	if (a->flags & AF_DEGRADED_MODE) {
		vi->status = ATTO_STS_DEGRADED;
		return false;
	}

	if (vi->function != VDA_FUNC_SCSI)
		clear_vda_request(rq);

	rq->vrq->scsi.function = vi->function;
	rq->interrupt_cb = esas2r_complete_vda_ioctl;
	rq->interrupt_cx = vi;

	switch (vi->function) {
	case VDA_FUNC_FLASH:

		if (vi->cmd.flash.sub_func != VDA_FLASH_FREAD
		    && vi->cmd.flash.sub_func != VDA_FLASH_FWRITE
		    && vi->cmd.flash.sub_func != VDA_FLASH_FINFO) {
			vi->status = ATTO_STS_INV_FUNC;
			return false;
		}

		if (vi->cmd.flash.sub_func != VDA_FLASH_FINFO)
			datalen = vi->data_length;

		rq->vrq->flash.length = cpu_to_le32(datalen);
		rq->vrq->flash.sub_func = vi->cmd.flash.sub_func;

		memcpy(rq->vrq->flash.data.file.file_name,
		       vi->cmd.flash.data.file.file_name,
		       sizeof(vi->cmd.flash.data.file.file_name));

		firstsg = rq->vrq->flash.data.file.sge;
		break;

	case VDA_FUNC_CLI:

		datalen = vi->data_length;

		rq->vrq->cli.cmd_rsp_len =
			cpu_to_le32(vi->cmd.cli.cmd_rsp_len);
		rq->vrq->cli.length = cpu_to_le32(datalen);

		firstsg = rq->vrq->cli.sge;
		break;

	case VDA_FUNC_MGT:
	{
		u8 *cmdcurr_offset = sgc->cur_offset
				     - offsetof(struct atto_ioctl_vda, data)
				     + offsetof(struct atto_ioctl_vda, cmd)
				     + offsetof(struct atto_ioctl_vda_mgt_cmd,
						data);
		/*
		 * build the data payload SGL here first since
		 * esas2r_sgc_init() will modify the S/G list offset for the
		 * management SGL (which is built below where the data SGL is
		 * usually built).
		 */

		if (vi->data_length) {
			u32 payldlen = 0;

			if (vi->cmd.mgt.mgt_func == VDAMGT_DEV_HEALTH_REQ
			    || vi->cmd.mgt.mgt_func == VDAMGT_DEV_METRICS) {
				rq->vrq->mgt.payld_sglst_offset =
					(u8)offsetof(struct atto_vda_mgmt_req,
						     payld_sge);

				payldlen = vi->data_length;
				datalen = vi->cmd.mgt.data_length;
			} else if (vi->cmd.mgt.mgt_func == VDAMGT_DEV_INFO2
				   || vi->cmd.mgt.mgt_func ==
				   VDAMGT_DEV_INFO2_BYADDR) {
				datalen = vi->data_length;
				cmdcurr_offset = sgc->cur_offset;
			} else {
				vi->status = ATTO_STS_INV_PARAM;
				return false;
			}

			/* Setup the length so building the payload SGL works */
			rq->vrq->mgt.length = cpu_to_le32(datalen);

			if (payldlen) {
				rq->vrq->mgt.payld_length =
					cpu_to_le32(payldlen);

				esas2r_sgc_init(sgc, a, rq,
						rq->vrq->mgt.payld_sge);
				sgc->length = payldlen;

				if (!esas2r_build_sg_list(a, rq, sgc)) {
					vi->status = ATTO_STS_OUT_OF_RSRC;
					return false;
				}
			}
		} else {
			datalen = vi->cmd.mgt.data_length;

			rq->vrq->mgt.length = cpu_to_le32(datalen);
		}

		/*
		 * Now that the payload SGL is built, if any, setup to build
		 * the management SGL.
		 */
		firstsg = rq->vrq->mgt.sge;
		sgc->cur_offset = cmdcurr_offset;

		/* Finish initializing the management request. */
		rq->vrq->mgt.mgt_func = vi->cmd.mgt.mgt_func;
		rq->vrq->mgt.scan_generation = vi->cmd.mgt.scan_generation;
		rq->vrq->mgt.dev_index =
			cpu_to_le32(vi->cmd.mgt.dev_index);

		esas2r_nuxi_mgt_data(rq->vrq->mgt.mgt_func, &vi->cmd.mgt.data);
		break;
	}

	case VDA_FUNC_CFG:

		if (vi->data_length
		    || vi->cmd.cfg.data_length == 0) {
			vi->status = ATTO_STS_INV_PARAM;
			return false;
		}

		if (vi->cmd.cfg.cfg_func == VDA_CFG_INIT) {
			vi->status = ATTO_STS_INV_FUNC;
			return false;
		}

		rq->vrq->cfg.sub_func = vi->cmd.cfg.cfg_func;
		rq->vrq->cfg.length = cpu_to_le32(vi->cmd.cfg.data_length);

		if (vi->cmd.cfg.cfg_func == VDA_CFG_GET_INIT) {
			memcpy(&rq->vrq->cfg.data,
			       &vi->cmd.cfg.data,
			       vi->cmd.cfg.data_length);

			esas2r_nuxi_cfg_data(rq->vrq->cfg.sub_func,
					     &rq->vrq->cfg.data);
		} else {
			vi->status = ATTO_STS_INV_FUNC;

			return false;
		}

		break;

	case VDA_FUNC_GSV:

		vi->cmd.gsv.rsp_len = vercnt;

		memcpy(vi->cmd.gsv.version_info, esas2r_vdaioctl_versions,
		       vercnt);

		vi->vda_status = RS_SUCCESS;
		break;

	default:

		vi->status = ATTO_STS_INV_FUNC;
		return false;
	}

	if (datalen) {
		esas2r_sgc_init(sgc, a, rq, firstsg);
		sgc->length = datalen;

		if (!esas2r_build_sg_list(a, rq, sgc)) {
			vi->status = ATTO_STS_OUT_OF_RSRC;
			return false;
		}
	}

	esas2r_start_request(a, rq);

	return true;
}

static void esas2r_complete_vda_ioctl(struct esas2r_adapter *a,
				      struct esas2r_request *rq)
{
	struct atto_ioctl_vda *vi = (struct atto_ioctl_vda *)rq->interrupt_cx;

	vi->vda_status = rq->req_stat;

	switch (vi->function) {
	case VDA_FUNC_FLASH:

		if (vi->cmd.flash.sub_func == VDA_FLASH_FINFO
		    || vi->cmd.flash.sub_func == VDA_FLASH_FREAD)
			vi->cmd.flash.data.file.file_size =
				le32_to_cpu(rq->func_rsp.flash_rsp.file_size);

		break;

	case VDA_FUNC_MGT:

		vi->cmd.mgt.scan_generation =
			rq->func_rsp.mgt_rsp.scan_generation;
		vi->cmd.mgt.dev_index = le16_to_cpu(
			rq->func_rsp.mgt_rsp.dev_index);

		if (vi->data_length == 0)
			vi->cmd.mgt.data_length =
				le32_to_cpu(rq->func_rsp.mgt_rsp.length);

		esas2r_nuxi_mgt_data(rq->vrq->mgt.mgt_func, &vi->cmd.mgt.data);
		break;

	case VDA_FUNC_CFG:

		if (vi->cmd.cfg.cfg_func == VDA_CFG_GET_INIT) {
			struct atto_ioctl_vda_cfg_cmd *cfg = &vi->cmd.cfg;
			struct atto_vda_cfg_rsp *rsp = &rq->func_rsp.cfg_rsp;

			cfg->data_length =
				cpu_to_le32(sizeof(struct atto_vda_cfg_init));
			cfg->data.init.vda_version =
				le32_to_cpu(rsp->vda_version);
			cfg->data.init.fw_build = rsp->fw_build;

			sprintf((char *)&cfg->data.init.fw_release,
				"%1d.%02d",
				(int)LOBYTE(le16_to_cpu(rsp->fw_release)),
				(int)HIBYTE(le16_to_cpu(rsp->fw_release)));

			if (LOWORD(LOBYTE(cfg->data.init.fw_build)) == 'A')
				cfg->data.init.fw_version =
					cfg->data.init.fw_build;
			else
				cfg->data.init.fw_version =
					cfg->data.init.fw_release;
		} else {
			esas2r_nuxi_cfg_data(rq->vrq->cfg.sub_func,
					     &vi->cmd.cfg.data);
		}

		break;

	case VDA_FUNC_CLI:

		vi->cmd.cli.cmd_rsp_len =
			le32_to_cpu(rq->func_rsp.cli_rsp.cmd_rsp_len);
		break;

	default:

		break;
	}
}

/* Build a flash VDA request. */
void esas2r_build_flash_req(struct esas2r_adapter *a,
			    struct esas2r_request *rq,
			    u8 sub_func,
			    u8 cksum,
			    u32 addr,
			    u32 length)
{
	struct atto_vda_flash_req *vrq = &rq->vrq->flash;

	clear_vda_request(rq);

	rq->vrq->scsi.function = VDA_FUNC_FLASH;

	if (sub_func == VDA_FLASH_BEGINW
	    || sub_func == VDA_FLASH_WRITE
	    || sub_func == VDA_FLASH_READ)
		vrq->sg_list_offset = (u8)offsetof(struct atto_vda_flash_req,
						   data.sge);

	vrq->length = cpu_to_le32(length);
	vrq->flash_addr = cpu_to_le32(addr);
	vrq->checksum = cksum;
	vrq->sub_func = sub_func;
}

/* Build a VDA management request. */
void esas2r_build_mgt_req(struct esas2r_adapter *a,
			  struct esas2r_request *rq,
			  u8 sub_func,
			  u8 scan_gen,
			  u16 dev_index,
			  u32 length,
			  void *data)
{
	struct atto_vda_mgmt_req *vrq = &rq->vrq->mgt;

	clear_vda_request(rq);

	rq->vrq->scsi.function = VDA_FUNC_MGT;

	vrq->mgt_func = sub_func;
	vrq->scan_generation = scan_gen;
	vrq->dev_index = cpu_to_le16(dev_index);
	vrq->length = cpu_to_le32(length);

	if (vrq->length) {
		if (a->flags & AF_LEGACY_SGE_MODE) {
			vrq->sg_list_offset = (u8)offsetof(
				struct atto_vda_mgmt_req, sge);

			vrq->sge[0].length = cpu_to_le32(SGE_LAST | length);
			vrq->sge[0].address = cpu_to_le64(
				rq->vrq_md->phys_addr +
				sizeof(union atto_vda_req));
		} else {
			vrq->sg_list_offset = (u8)offsetof(
				struct atto_vda_mgmt_req, prde);

			vrq->prde[0].ctl_len = cpu_to_le32(length);
			vrq->prde[0].address = cpu_to_le64(
				rq->vrq_md->phys_addr +
				sizeof(union atto_vda_req));
		}
	}

	if (data) {
		esas2r_nuxi_mgt_data(sub_func, data);

		memcpy(&rq->vda_rsp_data->mgt_data.data.bytes[0], data,
		       length);
	}
}

/* Build a VDA asyncronous event (AE) request. */
void esas2r_build_ae_req(struct esas2r_adapter *a, struct esas2r_request *rq)
{
	struct atto_vda_ae_req *vrq = &rq->vrq->ae;

	clear_vda_request(rq);

	rq->vrq->scsi.function = VDA_FUNC_AE;

	vrq->length = cpu_to_le32(sizeof(struct atto_vda_ae_data));

	if (a->flags & AF_LEGACY_SGE_MODE) {
		vrq->sg_list_offset =
			(u8)offsetof(struct atto_vda_ae_req, sge);
		vrq->sge[0].length = cpu_to_le32(SGE_LAST | vrq->length);
		vrq->sge[0].address = cpu_to_le64(
			rq->vrq_md->phys_addr +
			sizeof(union atto_vda_req));
	} else {
		vrq->sg_list_offset = (u8)offsetof(struct atto_vda_ae_req,
						   prde);
		vrq->prde[0].ctl_len = cpu_to_le32(vrq->length);
		vrq->prde[0].address = cpu_to_le64(
			rq->vrq_md->phys_addr +
			sizeof(union atto_vda_req));
	}
}

/* Build a VDA CLI request. */
void esas2r_build_cli_req(struct esas2r_adapter *a,
			  struct esas2r_request *rq,
			  u32 length,
			  u32 cmd_rsp_len)
{
	struct atto_vda_cli_req *vrq = &rq->vrq->cli;

	clear_vda_request(rq);

	rq->vrq->scsi.function = VDA_FUNC_CLI;

	vrq->length = cpu_to_le32(length);
	vrq->cmd_rsp_len = cpu_to_le32(cmd_rsp_len);
	vrq->sg_list_offset = (u8)offsetof(struct atto_vda_cli_req, sge);
}

/* Build a VDA IOCTL request. */
void esas2r_build_ioctl_req(struct esas2r_adapter *a,
			    struct esas2r_request *rq,
			    u32 length,
			    u8 sub_func)
{
	struct atto_vda_ioctl_req *vrq = &rq->vrq->ioctl;

	clear_vda_request(rq);

	rq->vrq->scsi.function = VDA_FUNC_IOCTL;

	vrq->length = cpu_to_le32(length);
	vrq->sub_func = sub_func;
	vrq->sg_list_offset = (u8)offsetof(struct atto_vda_ioctl_req, sge);
}

/* Build a VDA configuration request. */
void esas2r_build_cfg_req(struct esas2r_adapter *a,
			  struct esas2r_request *rq,
			  u8 sub_func,
			  u32 length,
			  void *data)
{
	struct atto_vda_cfg_req *vrq = &rq->vrq->cfg;

	clear_vda_request(rq);

	rq->vrq->scsi.function = VDA_FUNC_CFG;

	vrq->sub_func = sub_func;
	vrq->length = cpu_to_le32(length);

	if (data) {
		esas2r_nuxi_cfg_data(sub_func, data);

		memcpy(&vrq->data, data, length);
	}
}

static void clear_vda_request(struct esas2r_request *rq)
{
	u32 handle = rq->vrq->scsi.handle;

	memset(rq->vrq, 0, sizeof(*rq->vrq));

	rq->vrq->scsi.handle = handle;

	rq->req_stat = RS_PENDING;

	/* since the data buffer is separate clear that too */

	memset(rq->data_buf, 0, ESAS2R_DATA_BUF_LEN);

	/*
	 * Setup next and prev pointer in case the request is not going through
	 * esas2r_start_request().
	 */

	INIT_LIST_HEAD(&rq->req_list);
}
