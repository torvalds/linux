/*
 * This module provides common API for accessing firmware configuration pages
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_base.c
 * Copyright (C) 2012-2014  LSI Corporation
 * Copyright (C) 2013-2014 Avago Technologies
 *  (mailto: MPT-FusionLinux.pdl@avagotech.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.

 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "mpt3sas_base.h"

/* local definitions */

/* Timeout for config page request (in seconds) */
#define MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT 15

/* Common sgl flags for READING a config page. */
#define MPT3_CONFIG_COMMON_SGLFLAGS ((MPI2_SGE_FLAGS_SIMPLE_ELEMENT | \
	MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER \
	| MPI2_SGE_FLAGS_END_OF_LIST) << MPI2_SGE_FLAGS_SHIFT)

/* Common sgl flags for WRITING a config page. */
#define MPT3_CONFIG_COMMON_WRITE_SGLFLAGS ((MPI2_SGE_FLAGS_SIMPLE_ELEMENT | \
	MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER \
	| MPI2_SGE_FLAGS_END_OF_LIST | MPI2_SGE_FLAGS_HOST_TO_IOC) \
	<< MPI2_SGE_FLAGS_SHIFT)

/**
 * struct config_request - obtain dma memory via routine
 * @sz: size
 * @page: virt pointer
 * @page_dma: phys pointer
 *
 */
struct config_request {
	u16			sz;
	void			*page;
	dma_addr_t		page_dma;
};

/**
 * _config_display_some_debug - debug routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @calling_function_name: string pass from calling function
 * @mpi_reply: reply message frame
 * Context: none.
 *
 * Function for displaying debug info helpful when debugging issues
 * in this module.
 */
static void
_config_display_some_debug(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	char *calling_function_name, MPI2DefaultReply_t *mpi_reply)
{
	Mpi2ConfigRequest_t *mpi_request;
	char *desc = NULL;

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	switch (mpi_request->Header.PageType & MPI2_CONFIG_PAGETYPE_MASK) {
	case MPI2_CONFIG_PAGETYPE_IO_UNIT:
		desc = "io_unit";
		break;
	case MPI2_CONFIG_PAGETYPE_IOC:
		desc = "ioc";
		break;
	case MPI2_CONFIG_PAGETYPE_BIOS:
		desc = "bios";
		break;
	case MPI2_CONFIG_PAGETYPE_RAID_VOLUME:
		desc = "raid_volume";
		break;
	case MPI2_CONFIG_PAGETYPE_MANUFACTURING:
		desc = "manufacturing";
		break;
	case MPI2_CONFIG_PAGETYPE_RAID_PHYSDISK:
		desc = "physdisk";
		break;
	case MPI2_CONFIG_PAGETYPE_EXTENDED:
		switch (mpi_request->ExtPageType) {
		case MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT:
			desc = "sas_io_unit";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_SAS_EXPANDER:
			desc = "sas_expander";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_SAS_DEVICE:
			desc = "sas_device";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_SAS_PHY:
			desc = "sas_phy";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_LOG:
			desc = "log";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_ENCLOSURE:
			desc = "enclosure";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_RAID_CONFIG:
			desc = "raid_config";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_DRIVER_MAPPING:
			desc = "driver_mapping";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_SAS_PORT:
			desc = "sas_port";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_EXT_MANUFACTURING:
			desc = "ext_manufacturing";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_PCIE_IO_UNIT:
			desc = "pcie_io_unit";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_PCIE_SWITCH:
			desc = "pcie_switch";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_PCIE_DEVICE:
			desc = "pcie_device";
			break;
		case MPI2_CONFIG_EXTPAGETYPE_PCIE_LINK:
			desc = "pcie_link";
			break;
		}
		break;
	}

	if (!desc)
		return;

	ioc_info(ioc, "%s: %s(%d), action(%d), form(0x%08x), smid(%d)\n",
		 calling_function_name, desc,
		 mpi_request->Header.PageNumber, mpi_request->Action,
		 le32_to_cpu(mpi_request->PageAddress), smid);

	if (!mpi_reply)
		return;

	if (mpi_reply->IOCStatus || mpi_reply->IOCLogInfo)
		ioc_info(ioc, "\tiocstatus(0x%04x), loginfo(0x%08x)\n",
			 le16_to_cpu(mpi_reply->IOCStatus),
			 le32_to_cpu(mpi_reply->IOCLogInfo));
}

/**
 * _config_alloc_config_dma_memory - obtain physical memory
 * @ioc: per adapter object
 * @mem: struct config_request
 *
 * A wrapper for obtaining dma-able memory for config page request.
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_config_alloc_config_dma_memory(struct MPT3SAS_ADAPTER *ioc,
	struct config_request *mem)
{
	int r = 0;

	if (mem->sz > ioc->config_page_sz) {
		mem->page = dma_alloc_coherent(&ioc->pdev->dev, mem->sz,
		    &mem->page_dma, GFP_KERNEL);
		if (!mem->page) {
			ioc_err(ioc, "%s: dma_alloc_coherent failed asking for (%d) bytes!!\n",
				__func__, mem->sz);
			r = -ENOMEM;
		}
	} else { /* use tmp buffer if less than 512 bytes */
		mem->page = ioc->config_page;
		mem->page_dma = ioc->config_page_dma;
	}
	ioc->config_vaddr = mem->page;
	return r;
}

/**
 * _config_free_config_dma_memory - wrapper to free the memory
 * @ioc: per adapter object
 * @mem: struct config_request
 *
 * A wrapper to free dma-able memory when using _config_alloc_config_dma_memory.
 *
 * Return: 0 for success, non-zero for failure.
 */
static void
_config_free_config_dma_memory(struct MPT3SAS_ADAPTER *ioc,
	struct config_request *mem)
{
	if (mem->sz > ioc->config_page_sz)
		dma_free_coherent(&ioc->pdev->dev, mem->sz, mem->page,
		    mem->page_dma);
}

/**
 * mpt3sas_config_done - config page completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: none.
 *
 * The callback handler when using _config_request.
 *
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
u8
mpt3sas_config_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	if (ioc->config_cmds.status == MPT3_CMD_NOT_USED)
		return 1;
	if (ioc->config_cmds.smid != smid)
		return 1;
	ioc->config_cmds.status |= MPT3_CMD_COMPLETE;
	mpi_reply =  mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply) {
		ioc->config_cmds.status |= MPT3_CMD_REPLY_VALID;
		memcpy(ioc->config_cmds.reply, mpi_reply,
		    mpi_reply->MsgLength*4);
	}
	ioc->config_cmds.status &= ~MPT3_CMD_PENDING;
	if (ioc->logging_level & MPT_DEBUG_CONFIG)
		_config_display_some_debug(ioc, smid, "config_done", mpi_reply);
	ioc->config_cmds.smid = USHRT_MAX;
	complete(&ioc->config_cmds.done);
	return 1;
}

/**
 * _config_request - main routine for sending config page requests
 * @ioc: per adapter object
 * @mpi_request: request message frame
 * @mpi_reply: reply mf payload returned from firmware
 * @timeout: timeout in seconds
 * @config_page: contents of the config page
 * @config_page_sz: size of config page
 * Context: sleep
 *
 * A generic API for config page requests to firmware.
 *
 * The ioc->config_cmds.status flag should be MPT3_CMD_NOT_USED before calling
 * this API.
 *
 * The callback index is set inside `ioc->config_cb_idx.
 *
 * Return: 0 for success, non-zero for failure.
 */
static int
_config_request(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigRequest_t
	*mpi_request, Mpi2ConfigReply_t *mpi_reply, int timeout,
	void *config_page, u16 config_page_sz)
{
	u16 smid;
	Mpi2ConfigRequest_t *config_request;
	int r;
	u8 retry_count, issue_host_reset = 0;
	struct config_request mem;
	u32 ioc_status = UINT_MAX;

	mutex_lock(&ioc->config_cmds.mutex);
	if (ioc->config_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: config_cmd in use\n", __func__);
		mutex_unlock(&ioc->config_cmds.mutex);
		return -EAGAIN;
	}

	retry_count = 0;
	memset(&mem, 0, sizeof(struct config_request));

	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;

	if (config_page) {
		mpi_request->Header.PageVersion = mpi_reply->Header.PageVersion;
		mpi_request->Header.PageNumber = mpi_reply->Header.PageNumber;
		mpi_request->Header.PageType = mpi_reply->Header.PageType;
		mpi_request->Header.PageLength = mpi_reply->Header.PageLength;
		mpi_request->ExtPageLength = mpi_reply->ExtPageLength;
		mpi_request->ExtPageType = mpi_reply->ExtPageType;
		if (mpi_request->Header.PageLength)
			mem.sz = mpi_request->Header.PageLength * 4;
		else
			mem.sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r != 0)
			goto out;
		if (mpi_request->Action ==
		    MPI2_CONFIG_ACTION_PAGE_WRITE_CURRENT ||
		    mpi_request->Action ==
		    MPI2_CONFIG_ACTION_PAGE_WRITE_NVRAM) {
			ioc->base_add_sg_single(&mpi_request->PageBufferSGE,
			    MPT3_CONFIG_COMMON_WRITE_SGLFLAGS | mem.sz,
			    mem.page_dma);
			memcpy(mem.page, config_page, min_t(u16, mem.sz,
			    config_page_sz));
		} else {
			memset(config_page, 0, config_page_sz);
			ioc->base_add_sg_single(&mpi_request->PageBufferSGE,
			    MPT3_CONFIG_COMMON_SGLFLAGS | mem.sz, mem.page_dma);
			memset(mem.page, 0, min_t(u16, mem.sz, config_page_sz));
		}
	}

 retry_config:
	if (retry_count) {
		if (retry_count > 2) { /* attempt only 2 retries */
			r = -EFAULT;
			goto free_mem;
		}
		ioc_info(ioc, "%s: attempting retry (%d)\n",
			 __func__, retry_count);
	}

	r = mpt3sas_wait_for_ioc(ioc, MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto free_mem;

	smid = mpt3sas_base_get_smid(ioc, ioc->config_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		ioc->config_cmds.status = MPT3_CMD_NOT_USED;
		r = -EAGAIN;
		goto free_mem;
	}

	r = 0;
	memset(mpi_reply, 0, sizeof(Mpi2ConfigReply_t));
	ioc->config_cmds.status = MPT3_CMD_PENDING;
	config_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->config_cmds.smid = smid;
	memcpy(config_request, mpi_request, sizeof(Mpi2ConfigRequest_t));
	if (ioc->logging_level & MPT_DEBUG_CONFIG)
		_config_display_some_debug(ioc, smid, "config_request", NULL);
	init_completion(&ioc->config_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->config_cmds.done, timeout*HZ);
	if (!(ioc->config_cmds.status & MPT3_CMD_COMPLETE)) {
		if (!(ioc->logging_level & MPT_DEBUG_CONFIG))
			_config_display_some_debug(ioc,
			    smid, "config_request", NULL);
		mpt3sas_base_check_cmd_timeout(ioc,
			ioc->config_cmds.status, mpi_request,
			sizeof(Mpi2ConfigRequest_t)/4);
		retry_count++;
		if (ioc->config_cmds.smid == smid)
			mpt3sas_base_free_smid(ioc, smid);
		if ((ioc->shost_recovery) || (ioc->config_cmds.status &
		    MPT3_CMD_RESET) || ioc->pci_error_recovery)
			goto retry_config;
		issue_host_reset = 1;
		r = -EFAULT;
		goto free_mem;
	}

	if (ioc->config_cmds.status & MPT3_CMD_REPLY_VALID) {
		memcpy(mpi_reply, ioc->config_cmds.reply,
		    sizeof(Mpi2ConfigReply_t));

		/* Reply Frame Sanity Checks to workaround FW issues */
		if ((mpi_request->Header.PageType & 0xF) !=
		    (mpi_reply->Header.PageType & 0xF)) {
			if (!(ioc->logging_level & MPT_DEBUG_CONFIG))
				_config_display_some_debug(ioc,
				    smid, "config_request", NULL);
			_debug_dump_mf(mpi_request, ioc->request_sz/4);
			_debug_dump_reply(mpi_reply, ioc->reply_sz/4);
			panic("%s: %s: Firmware BUG: mpi_reply mismatch: Requested PageType(0x%02x) Reply PageType(0x%02x)\n",
			      ioc->name, __func__,
			      mpi_request->Header.PageType & 0xF,
			      mpi_reply->Header.PageType & 0xF);
		}

		if (((mpi_request->Header.PageType & 0xF) ==
		    MPI2_CONFIG_PAGETYPE_EXTENDED) &&
		    mpi_request->ExtPageType != mpi_reply->ExtPageType) {
			if (!(ioc->logging_level & MPT_DEBUG_CONFIG))
				_config_display_some_debug(ioc,
				    smid, "config_request", NULL);
			_debug_dump_mf(mpi_request, ioc->request_sz/4);
			_debug_dump_reply(mpi_reply, ioc->reply_sz/4);
			panic("%s: %s: Firmware BUG: mpi_reply mismatch: Requested ExtPageType(0x%02x) Reply ExtPageType(0x%02x)\n",
			      ioc->name, __func__,
			      mpi_request->ExtPageType,
			      mpi_reply->ExtPageType);
		}
		ioc_status = le16_to_cpu(mpi_reply->IOCStatus)
		    & MPI2_IOCSTATUS_MASK;
	}

	if (retry_count)
		ioc_info(ioc, "%s: retry (%d) completed!!\n",
			 __func__, retry_count);

	if ((ioc_status == MPI2_IOCSTATUS_SUCCESS) &&
	    config_page && mpi_request->Action ==
	    MPI2_CONFIG_ACTION_PAGE_READ_CURRENT) {
		u8 *p = (u8 *)mem.page;

		/* Config Page Sanity Checks to workaround FW issues */
		if (p) {
			if ((mpi_request->Header.PageType & 0xF) !=
			    (p[3] & 0xF)) {
				if (!(ioc->logging_level & MPT_DEBUG_CONFIG))
					_config_display_some_debug(ioc,
					    smid, "config_request", NULL);
				_debug_dump_mf(mpi_request, ioc->request_sz/4);
				_debug_dump_reply(mpi_reply, ioc->reply_sz/4);
				_debug_dump_config(p, min_t(u16, mem.sz,
				    config_page_sz)/4);
				panic("%s: %s: Firmware BUG: config page mismatch: Requested PageType(0x%02x) Reply PageType(0x%02x)\n",
				      ioc->name, __func__,
				      mpi_request->Header.PageType & 0xF,
				      p[3] & 0xF);
			}

			if (((mpi_request->Header.PageType & 0xF) ==
			    MPI2_CONFIG_PAGETYPE_EXTENDED) &&
			    (mpi_request->ExtPageType != p[6])) {
				if (!(ioc->logging_level & MPT_DEBUG_CONFIG))
					_config_display_some_debug(ioc,
					    smid, "config_request", NULL);
				_debug_dump_mf(mpi_request, ioc->request_sz/4);
				_debug_dump_reply(mpi_reply, ioc->reply_sz/4);
				_debug_dump_config(p, min_t(u16, mem.sz,
				    config_page_sz)/4);
				panic("%s: %s: Firmware BUG: config page mismatch: Requested ExtPageType(0x%02x) Reply ExtPageType(0x%02x)\n",
				      ioc->name, __func__,
				      mpi_request->ExtPageType, p[6]);
			}
		}
		memcpy(config_page, mem.page, min_t(u16, mem.sz,
		    config_page_sz));
	}

 free_mem:
	if (config_page)
		_config_free_config_dma_memory(ioc, &mem);
 out:
	ioc->config_cmds.status = MPT3_CMD_NOT_USED;
	mutex_unlock(&ioc->config_cmds.mutex);

	if (issue_host_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	return r;
}

/**
 * mpt3sas_config_get_manufacturing_pg0 - obtain manufacturing page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_manufacturing_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ManufacturingPage0_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_MANUFACTURING;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_MANUFACTURING0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_manufacturing_pg7 - obtain manufacturing page 7
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_manufacturing_pg7(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ManufacturingPage7_t *config_page,
	u16 sz)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_MANUFACTURING;
	mpi_request.Header.PageNumber = 7;
	mpi_request.Header.PageVersion = MPI2_MANUFACTURING7_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sz);
 out:
	return r;
}

/**
 * mpt3sas_config_get_manufacturing_pg10 - obtain manufacturing page 10
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_manufacturing_pg10(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply,
	struct Mpi2ManufacturingPage10_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_MANUFACTURING;
	mpi_request.Header.PageNumber = 10;
	mpi_request.Header.PageVersion = MPI2_MANUFACTURING0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_manufacturing_pg11 - obtain manufacturing page 11
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_manufacturing_pg11(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply,
	struct Mpi2ManufacturingPage11_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_MANUFACTURING;
	mpi_request.Header.PageNumber = 11;
	mpi_request.Header.PageVersion = MPI2_MANUFACTURING0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_set_manufacturing_pg11 - set manufacturing page 11
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_set_manufacturing_pg11(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply,
	struct Mpi2ManufacturingPage11_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_MANUFACTURING;
	mpi_request.Header.PageNumber = 11;
	mpi_request.Header.PageVersion = MPI2_MANUFACTURING0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_bios_pg2 - obtain bios page 2
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_bios_pg2(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2BiosPage2_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_BIOS;
	mpi_request.Header.PageNumber = 2;
	mpi_request.Header.PageVersion = MPI2_BIOSPAGE2_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_bios_pg3 - obtain bios page 3
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_bios_pg3(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2BiosPage3_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_BIOS;
	mpi_request.Header.PageNumber = 3;
	mpi_request.Header.PageVersion = MPI2_BIOSPAGE3_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_iounit_pg0 - obtain iounit page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_iounit_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage0_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_IOUNITPAGE0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_iounit_pg1 - obtain iounit page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_iounit_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage1_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_IOUNITPAGE1_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_set_iounit_pg1 - set iounit page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_set_iounit_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage1_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_IOUNITPAGE1_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_iounit_pg3 - obtain iounit page 3
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_iounit_pg3(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage3_t *config_page, u16 sz)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	mpi_request.Header.PageNumber = 3;
	mpi_request.Header.PageVersion = MPI2_IOUNITPAGE3_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page, sz);
 out:
	return r;
}

/**
 * mpt3sas_config_get_iounit_pg8 - obtain iounit page 8
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_iounit_pg8(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage8_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	mpi_request.Header.PageNumber = 8;
	mpi_request.Header.PageVersion = MPI2_IOUNITPAGE8_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_ioc_pg8 - obtain ioc page 8
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_ioc_pg8(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOCPage8_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IOC;
	mpi_request.Header.PageNumber = 8;
	mpi_request.Header.PageVersion = MPI2_IOCPAGE8_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}
/**
 * mpt3sas_config_get_ioc_pg1 - obtain ioc page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_ioc_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOCPage1_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IOC;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_IOCPAGE8_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_set_ioc_pg1 - modify ioc page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_set_ioc_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOCPage1_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IOC;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_IOCPAGE8_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_sas_device_pg0 - obtain sas device page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: device handle
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_sas_device_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasDevicePage0_t *config_page,
	u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_DEVICE;
	mpi_request.Header.PageVersion = MPI2_SASDEVICE0_PAGEVERSION;
	mpi_request.Header.PageNumber = 0;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_sas_device_pg1 - obtain sas device page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: device handle
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_sas_device_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasDevicePage1_t *config_page,
	u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_DEVICE;
	mpi_request.Header.PageVersion = MPI2_SASDEVICE1_PAGEVERSION;
	mpi_request.Header.PageNumber = 1;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_pcie_device_pg0 - obtain pcie device page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: device handle
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_pcie_device_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi26PCIeDevicePage0_t *config_page,
	u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_PCIE_DEVICE;
	mpi_request.Header.PageVersion = MPI26_PCIEDEVICE0_PAGEVERSION;
	mpi_request.Header.PageNumber = 0;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
			MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
			MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
			sizeof(*config_page));
out:
	return r;
}

/**
 * mpt3sas_config_get_pcie_device_pg2 - obtain pcie device page 2
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: device handle
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_pcie_device_pg2(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi26PCIeDevicePage2_t *config_page,
	u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_PCIE_DEVICE;
	mpi_request.Header.PageVersion = MPI26_PCIEDEVICE2_PAGEVERSION;
	mpi_request.Header.PageNumber = 2;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
			MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
			MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
			sizeof(*config_page));
out:
	return r;
}

/**
 * mpt3sas_config_get_number_hba_phys - obtain number of phys on the host
 * @ioc: per adapter object
 * @num_phys: pointer returned with the number of phys
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_number_hba_phys(struct MPT3SAS_ADAPTER *ioc, u8 *num_phys)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	u16 ioc_status;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t config_page;

	*num_phys = 0;
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASIOUNITPAGE0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, &config_page,
	    sizeof(Mpi2SasIOUnitPage0_t));
	if (!r) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_SUCCESS)
			*num_phys = config_page.NumPhys;
	}
 out:
	return r;
}

/**
 * mpt3sas_config_get_sas_iounit_pg0 - obtain sas iounit page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Calling function should call config_get_number_hba_phys prior to
 * this function, so enough memory is allocated for config_page.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_sas_iounit_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasIOUnitPage0_t *config_page,
	u16 sz)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASIOUNITPAGE0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page, sz);
 out:
	return r;
}

/**
 * mpt3sas_config_get_sas_iounit_pg1 - obtain sas iounit page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Calling function should call config_get_number_hba_phys prior to
 * this function, so enough memory is allocated for config_page.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_sas_iounit_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasIOUnitPage1_t *config_page,
	u16 sz)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_SASIOUNITPAGE1_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page, sz);
 out:
	return r;
}

/**
 * mpt3sas_config_set_sas_iounit_pg1 - send sas iounit page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Calling function should call config_get_number_hba_phys prior to
 * this function, so enough memory is allocated for config_page.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_set_sas_iounit_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasIOUnitPage1_t *config_page,
	u16 sz)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_SASIOUNITPAGE1_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	_config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page, sz);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_WRITE_NVRAM;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page, sz);
 out:
	return r;
}

/**
 * mpt3sas_config_get_expander_pg0 - obtain expander page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: expander handle
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_expander_pg0(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2ExpanderPage0_t *config_page, u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_EXPANDER;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASEXPANDER0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_expander_pg1 - obtain expander page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @phy_number: phy number
 * @handle: expander handle
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_expander_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2ExpanderPage1_t *config_page, u32 phy_number,
	u16 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_EXPANDER;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_SASEXPANDER1_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress =
	    cpu_to_le32(MPI2_SAS_EXPAND_PGAD_FORM_HNDL_PHY_NUM |
	    (phy_number << MPI2_SAS_EXPAND_PGAD_PHYNUM_SHIFT) | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_enclosure_pg0 - obtain enclosure page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: expander handle
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_enclosure_pg0(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2SasEnclosurePage0_t *config_page, u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_ENCLOSURE;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASENCLOSURE0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_phy_pg0 - obtain phy page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @phy_number: phy number
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_phy_pg0(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2SasPhyPage0_t *config_page, u32 phy_number)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_PHY;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASPHY0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress =
	    cpu_to_le32(MPI2_SAS_PHY_PGAD_FORM_PHY_NUMBER | phy_number);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_phy_pg1 - obtain phy page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @phy_number: phy number
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_phy_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2SasPhyPage1_t *config_page, u32 phy_number)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_PHY;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_SASPHY1_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress =
	    cpu_to_le32(MPI2_SAS_PHY_PGAD_FORM_PHY_NUMBER | phy_number);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_raid_volume_pg1 - obtain raid volume page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: volume handle
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_raid_volume_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2RaidVolPage1_t *config_page, u32 form,
	u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_RAIDVOLPAGE1_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_number_pds - obtain number of phys disk assigned to volume
 * @ioc: per adapter object
 * @handle: volume handle
 * @num_pds: returns pds count
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_number_pds(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 *num_pds)
{
	Mpi2ConfigRequest_t mpi_request;
	Mpi2RaidVolPage0_t config_page;
	Mpi2ConfigReply_t mpi_reply;
	int r;
	u16 ioc_status;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	*num_pds = 0;
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_RAIDVOLPAGE0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress =
	    cpu_to_le32(MPI2_RAID_VOLUME_PGAD_FORM_HANDLE | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, &config_page,
	    sizeof(Mpi2RaidVolPage0_t));
	if (!r) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_SUCCESS)
			*num_pds = config_page.NumPhysDisks;
	}

 out:
	return r;
}

/**
 * mpt3sas_config_get_raid_volume_pg0 - obtain raid volume page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: volume handle
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_raid_volume_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2RaidVolPage0_t *config_page, u32 form,
	u32 handle, u16 sz)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_RAIDVOLPAGE0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page, sz);
 out:
	return r;
}

/**
 * mpt3sas_config_get_phys_disk_pg0 - obtain phys disk page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_PHYSDISKNUM, PHYSDISKNUM, DEVHANDLE
 * @form_specific: specific to the form
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_phys_disk_pg0(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2RaidPhysDiskPage0_t *config_page, u32 form,
	u32 form_specific)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;

	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_PHYSDISK;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_RAIDPHYSDISKPAGE0_PAGEVERSION;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | form_specific);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
	    sizeof(*config_page));
 out:
	return r;
}

/**
 * mpt3sas_config_get_volume_handle - returns volume handle for give hidden
 * raid components
 * @ioc: per adapter object
 * @pd_handle: phys disk handle
 * @volume_handle: volume handle
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_volume_handle(struct MPT3SAS_ADAPTER *ioc, u16 pd_handle,
	u16 *volume_handle)
{
	Mpi2RaidConfigurationPage0_t *config_page = NULL;
	Mpi2ConfigRequest_t mpi_request;
	Mpi2ConfigReply_t mpi_reply;
	int r, i, config_page_sz;
	u16 ioc_status;
	int config_num;
	u16 element_type;
	u16 phys_disk_dev_handle;

	*volume_handle = 0;
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_RAID_CONFIG;
	mpi_request.Header.PageVersion = MPI2_RAIDCONFIG0_PAGEVERSION;
	mpi_request.Header.PageNumber = 0;
	ioc->build_zero_len_sge_mpi(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, NULL, 0);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	config_page_sz = (le16_to_cpu(mpi_reply.ExtPageLength) * 4);
	config_page = kmalloc(config_page_sz, GFP_KERNEL);
	if (!config_page) {
		r = -1;
		goto out;
	}

	config_num = 0xff;
	while (1) {
		mpi_request.PageAddress = cpu_to_le32(config_num +
		    MPI2_RAID_PGAD_FORM_GET_NEXT_CONFIGNUM);
		r = _config_request(ioc, &mpi_request, &mpi_reply,
		    MPT3_CONFIG_PAGE_DEFAULT_TIMEOUT, config_page,
		    config_page_sz);
		if (r)
			goto out;
		r = -1;
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
			goto out;
		for (i = 0; i < config_page->NumElements; i++) {
			element_type = le16_to_cpu(config_page->
			    ConfigElement[i].ElementFlags) &
			    MPI2_RAIDCONFIG0_EFLAGS_MASK_ELEMENT_TYPE;
			if (element_type ==
			    MPI2_RAIDCONFIG0_EFLAGS_VOL_PHYS_DISK_ELEMENT ||
			    element_type ==
			    MPI2_RAIDCONFIG0_EFLAGS_OCE_ELEMENT) {
				phys_disk_dev_handle =
				    le16_to_cpu(config_page->ConfigElement[i].
				    PhysDiskDevHandle);
				if (phys_disk_dev_handle == pd_handle) {
					*volume_handle =
					    le16_to_cpu(config_page->
					    ConfigElement[i].VolDevHandle);
					r = 0;
					goto out;
				}
			} else if (element_type ==
			    MPI2_RAIDCONFIG0_EFLAGS_HOT_SPARE_ELEMENT) {
				*volume_handle = 0;
				r = 0;
				goto out;
			}
		}
		config_num = config_page->ConfigNum;
	}
 out:
	kfree(config_page);
	return r;
}

/**
 * mpt3sas_config_get_volume_wwid - returns wwid given the volume handle
 * @ioc: per adapter object
 * @volume_handle: volume handle
 * @wwid: volume wwid
 * Context: sleep.
 *
 * Return: 0 for success, non-zero for failure.
 */
int
mpt3sas_config_get_volume_wwid(struct MPT3SAS_ADAPTER *ioc, u16 volume_handle,
	u64 *wwid)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2RaidVolPage1_t raid_vol_pg1;

	*wwid = 0;
	if (!(mpt3sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &raid_vol_pg1, MPI2_RAID_VOLUME_PGAD_FORM_HANDLE,
	    volume_handle))) {
		*wwid = le64_to_cpu(raid_vol_pg1.WWID);
		return 0;
	} else
		return -1;
}
