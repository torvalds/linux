/*
 * This module provides common API for accessing firmware configuration pages
 *
 * This code is based on drivers/scsi/mpt2sas/mpt2_base.c
 * Copyright (C) 2007-2008  LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
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

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "mpt2sas_base.h"

/* local definitions */

/* Timeout for config page request (in seconds) */
#define MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT 15

/* Common sgl flags for READING a config page. */
#define MPT2_CONFIG_COMMON_SGLFLAGS ((MPI2_SGE_FLAGS_SIMPLE_ELEMENT | \
    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER \
    | MPI2_SGE_FLAGS_END_OF_LIST) << MPI2_SGE_FLAGS_SHIFT)

/* Common sgl flags for WRITING a config page. */
#define MPT2_CONFIG_COMMON_WRITE_SGLFLAGS ((MPI2_SGE_FLAGS_SIMPLE_ELEMENT | \
    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER \
    | MPI2_SGE_FLAGS_END_OF_LIST | MPI2_SGE_FLAGS_HOST_TO_IOC) \
    << MPI2_SGE_FLAGS_SHIFT)

/**
 * struct config_request - obtain dma memory via routine
 * @config_page_sz: size
 * @config_page: virt pointer
 * @config_page_dma: phys pointer
 *
 */
struct config_request{
	u16			config_page_sz;
	void			*config_page;
	dma_addr_t		config_page_dma;
};

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _config_display_some_debug - debug routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @calling_function_name: string pass from calling function
 * @mpi_reply: reply message frame
 * Context: none.
 *
 * Function for displaying debug info helpfull when debugging issues
 * in this module.
 */
static void
_config_display_some_debug(struct MPT2SAS_ADAPTER *ioc, u16 smid,
    char *calling_function_name, MPI2DefaultReply_t *mpi_reply)
{
	Mpi2ConfigRequest_t *mpi_request;
	char *desc = NULL;

	if (!(ioc->logging_level & MPT_DEBUG_CONFIG))
		return;

	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
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
		desc = "manufaucturing";
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
			desc = "driver_mappping";
			break;
		}
		break;
	}

	if (!desc)
		return;

	printk(MPT2SAS_DEBUG_FMT "%s: %s(%d), action(%d), form(0x%08x), "
	    "smid(%d)\n", ioc->name, calling_function_name, desc,
	    mpi_request->Header.PageNumber, mpi_request->Action,
	    le32_to_cpu(mpi_request->PageAddress), smid);

	if (!mpi_reply)
		return;

	if (mpi_reply->IOCStatus || mpi_reply->IOCLogInfo)
		printk(MPT2SAS_DEBUG_FMT
		    "\tiocstatus(0x%04x), loginfo(0x%08x)\n",
		    ioc->name, le16_to_cpu(mpi_reply->IOCStatus),
		    le32_to_cpu(mpi_reply->IOCLogInfo));
}
#endif

/**
 * mpt2sas_config_done - config page completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @VF_ID: virtual function id
 * @reply: reply message frame(lower 32bit addr)
 * Context: none.
 *
 * The callback handler when using _config_request.
 *
 * Return nothing.
 */
void
mpt2sas_config_done(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 VF_ID, u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	if (ioc->config_cmds.status == MPT2_CMD_NOT_USED)
		return;
	if (ioc->config_cmds.smid != smid)
		return;
	ioc->config_cmds.status |= MPT2_CMD_COMPLETE;
	mpi_reply =  mpt2sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply) {
		ioc->config_cmds.status |= MPT2_CMD_REPLY_VALID;
		memcpy(ioc->config_cmds.reply, mpi_reply,
		    mpi_reply->MsgLength*4);
	}
	ioc->config_cmds.status &= ~MPT2_CMD_PENDING;
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	_config_display_some_debug(ioc, smid, "config_done", mpi_reply);
#endif
	complete(&ioc->config_cmds.done);
}

/**
 * _config_request - main routine for sending config page requests
 * @ioc: per adapter object
 * @mpi_request: request message frame
 * @mpi_reply: reply mf payload returned from firmware
 * @timeout: timeout in seconds
 * Context: sleep, the calling function needs to acquire the config_cmds.mutex
 *
 * A generic API for config page requests to firmware.
 *
 * The ioc->config_cmds.status flag should be MPT2_CMD_NOT_USED before calling
 * this API.
 *
 * The callback index is set inside `ioc->config_cb_idx.
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_config_request(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigRequest_t
    *mpi_request, Mpi2ConfigReply_t *mpi_reply, int timeout)
{
	u16 smid;
	u32 ioc_state;
	unsigned long timeleft;
	Mpi2ConfigRequest_t *config_request;
	int r;
	u8 retry_count;
	u8 issue_reset;
	u16 wait_state_count;

	if (ioc->config_cmds.status != MPT2_CMD_NOT_USED) {
		printk(MPT2SAS_ERR_FMT "%s: config_cmd in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}
	retry_count = 0;

 retry_config:
	wait_state_count = 0;
	ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (wait_state_count++ == MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT) {
			printk(MPT2SAS_ERR_FMT
			    "%s: failed due to ioc not operational\n",
			    ioc->name, __func__);
			ioc->config_cmds.status = MPT2_CMD_NOT_USED;
			return -EFAULT;
		}
		ssleep(1);
		ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
		printk(MPT2SAS_INFO_FMT "%s: waiting for "
		    "operational state(count=%d)\n", ioc->name,
		    __func__, wait_state_count);
	}
	if (wait_state_count)
		printk(MPT2SAS_INFO_FMT "%s: ioc is operational\n",
		    ioc->name, __func__);

	smid = mpt2sas_base_get_smid(ioc, ioc->config_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		ioc->config_cmds.status = MPT2_CMD_NOT_USED;
		return -EAGAIN;
	}

	r = 0;
	memset(mpi_reply, 0, sizeof(Mpi2ConfigReply_t));
	ioc->config_cmds.status = MPT2_CMD_PENDING;
	config_request = mpt2sas_base_get_msg_frame(ioc, smid);
	ioc->config_cmds.smid = smid;
	memcpy(config_request, mpi_request, sizeof(Mpi2ConfigRequest_t));
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	_config_display_some_debug(ioc, smid, "config_request", NULL);
#endif
	mpt2sas_base_put_smid_default(ioc, smid, config_request->VF_ID);
	timeleft = wait_for_completion_timeout(&ioc->config_cmds.done,
	    timeout*HZ);
	if (!(ioc->config_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2ConfigRequest_t)/4);
		if (!(ioc->config_cmds.status & MPT2_CMD_RESET))
			issue_reset = 1;
		goto issue_host_reset;
	}
	if (ioc->config_cmds.status & MPT2_CMD_REPLY_VALID)
		memcpy(mpi_reply, ioc->config_cmds.reply,
		    sizeof(Mpi2ConfigReply_t));
	if (retry_count)
		printk(MPT2SAS_INFO_FMT "%s: retry completed!!\n",
		    ioc->name, __func__);
	ioc->config_cmds.status = MPT2_CMD_NOT_USED;
	return r;

 issue_host_reset:
	if (issue_reset)
		mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);
	ioc->config_cmds.status = MPT2_CMD_NOT_USED;
	if (!retry_count) {
		printk(MPT2SAS_INFO_FMT "%s: attempting retry\n",
		    ioc->name, __func__);
		retry_count++;
		goto retry_config;
	}
	return -EFAULT;
}

/**
 * _config_alloc_config_dma_memory - obtain physical memory
 * @ioc: per adapter object
 * @mem: struct config_request
 *
 * A wrapper for obtaining dma-able memory for config page request.
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_config_alloc_config_dma_memory(struct MPT2SAS_ADAPTER *ioc,
    struct config_request *mem)
{
	int r = 0;

	mem->config_page = pci_alloc_consistent(ioc->pdev, mem->config_page_sz,
	    &mem->config_page_dma);
	if (!mem->config_page)
		r = -ENOMEM;
	return r;
}

/**
 * _config_free_config_dma_memory - wrapper to free the memory
 * @ioc: per adapter object
 * @mem: struct config_request
 *
 * A wrapper to free dma-able memory when using _config_alloc_config_dma_memory.
 *
 * Returns 0 for success, non-zero for failure.
 */
static void
_config_free_config_dma_memory(struct MPT2SAS_ADAPTER *ioc,
    struct config_request *mem)
{
	pci_free_consistent(ioc->pdev, mem->config_page_sz, mem->config_page,
	    mem->config_page_dma);
}

/**
 * mpt2sas_config_get_manufacturing_pg0 - obtain manufacturing page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_manufacturing_pg0(struct MPT2SAS_ADAPTER *ioc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2ManufacturingPage0_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2ManufacturingPage0_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_MANUFACTURING;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_MANUFACTURING0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2ManufacturingPage0_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_bios_pg2 - obtain bios page 2
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_bios_pg2(struct MPT2SAS_ADAPTER *ioc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2BiosPage2_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2BiosPage2_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_BIOS;
	mpi_request.Header.PageNumber = 2;
	mpi_request.Header.PageVersion = MPI2_BIOSPAGE2_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2BiosPage2_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_bios_pg3 - obtain bios page 3
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_bios_pg3(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2BiosPage3_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2BiosPage3_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_BIOS;
	mpi_request.Header.PageNumber = 3;
	mpi_request.Header.PageVersion = MPI2_BIOSPAGE3_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2BiosPage3_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_iounit_pg0 - obtain iounit page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_iounit_pg0(struct MPT2SAS_ADAPTER *ioc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage0_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2IOUnitPage0_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_IOUNITPAGE0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2IOUnitPage0_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_iounit_pg1 - obtain iounit page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_iounit_pg1(struct MPT2SAS_ADAPTER *ioc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage1_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2IOUnitPage1_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_IOUNITPAGE1_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2IOUnitPage1_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_set_iounit_pg1 - set iounit page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_set_iounit_pg1(struct MPT2SAS_ADAPTER *ioc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage1_t config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_IOUNITPAGE1_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}

	memset(mem.config_page, 0, mem.config_page_sz);
	memcpy(mem.config_page, &config_page,
	    sizeof(Mpi2IOUnitPage1_t));

	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_WRITE_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_ioc_pg8 - obtain ioc page 8
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_ioc_pg8(struct MPT2SAS_ADAPTER *ioc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2IOCPage8_t *config_page)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2IOCPage8_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_IOC;
	mpi_request.Header.PageNumber = 8;
	mpi_request.Header.PageVersion = MPI2_IOCPAGE8_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2IOCPage8_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_sas_device_pg0 - obtain sas device page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: device handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_sas_device_pg0(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2SasDevicePage0_t *config_page, u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2SasDevicePage0_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_DEVICE;
	mpi_request.Header.PageVersion = MPI2_SASDEVICE0_PAGEVERSION;
	mpi_request.Header.PageNumber = 0;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.ExtPageLength = mpi_reply->ExtPageLength;
	mpi_request.ExtPageType = mpi_reply->ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2SasDevicePage0_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_sas_device_pg1 - obtain sas device page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: device handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_sas_device_pg1(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2SasDevicePage1_t *config_page, u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2SasDevicePage1_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_DEVICE;
	mpi_request.Header.PageVersion = MPI2_SASDEVICE1_PAGEVERSION;
	mpi_request.Header.PageNumber = 1;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.ExtPageLength = mpi_reply->ExtPageLength;
	mpi_request.ExtPageType = mpi_reply->ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2SasDevicePage1_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_number_hba_phys - obtain number of phys on the host
 * @ioc: per adapter object
 * @num_phys: pointer returned with the number of phys
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_number_hba_phys(struct MPT2SAS_ADAPTER *ioc, u8 *num_phys)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;
	u16 ioc_status;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t config_page;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASIOUNITPAGE0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply.Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply.Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply.Header.PageType;
	mpi_request.ExtPageLength = mpi_reply.ExtPageLength;
	mpi_request.ExtPageType = mpi_reply.ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply.ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
			memcpy(&config_page, mem.config_page,
			    min_t(u16, mem.config_page_sz,
			    sizeof(Mpi2SasIOUnitPage0_t)));
			*num_phys = config_page.NumPhys;
		}
	}

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_sas_iounit_pg0 - obtain sas iounit page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Calling function should call config_get_number_hba_phys prior to
 * this function, so enough memory is allocated for config_page.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_sas_iounit_pg0(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2SasIOUnitPage0_t *config_page, u16 sz)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sz);
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASIOUNITPAGE0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.ExtPageLength = mpi_reply->ExtPageLength;
	mpi_request.ExtPageType = mpi_reply->ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, sz, mem.config_page_sz));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_sas_iounit_pg1 - obtain sas iounit page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Calling function should call config_get_number_hba_phys prior to
 * this function, so enough memory is allocated for config_page.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_sas_iounit_pg1(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2SasIOUnitPage1_t *config_page, u16 sz)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sz);
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_SASIOUNITPAGE0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.ExtPageLength = mpi_reply->ExtPageLength;
	mpi_request.ExtPageType = mpi_reply->ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, sz, mem.config_page_sz));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_expander_pg0 - obtain expander page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: expander handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_expander_pg0(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2ExpanderPage0_t *config_page, u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2ExpanderPage0_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_EXPANDER;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASEXPANDER0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.ExtPageLength = mpi_reply->ExtPageLength;
	mpi_request.ExtPageType = mpi_reply->ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2ExpanderPage0_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_expander_pg1 - obtain expander page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @phy_number: phy number
 * @handle: expander handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_expander_pg1(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2ExpanderPage1_t *config_page, u32 phy_number,
    u16 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2ExpanderPage1_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_EXPANDER;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_SASEXPANDER1_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress =
	    cpu_to_le32(MPI2_SAS_EXPAND_PGAD_FORM_HNDL_PHY_NUM |
	    (phy_number << MPI2_SAS_EXPAND_PGAD_PHYNUM_SHIFT) | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.ExtPageLength = mpi_reply->ExtPageLength;
	mpi_request.ExtPageType = mpi_reply->ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2ExpanderPage1_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_enclosure_pg0 - obtain enclosure page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: expander handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_enclosure_pg0(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2SasEnclosurePage0_t *config_page, u32 form, u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2SasEnclosurePage0_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_ENCLOSURE;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASENCLOSURE0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.ExtPageLength = mpi_reply->ExtPageLength;
	mpi_request.ExtPageType = mpi_reply->ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2SasEnclosurePage0_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_phy_pg0 - obtain phy page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @phy_number: phy number
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_phy_pg0(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2SasPhyPage0_t *config_page, u32 phy_number)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2SasPhyPage0_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_PHY;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_SASPHY0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress =
	    cpu_to_le32(MPI2_SAS_PHY_PGAD_FORM_PHY_NUMBER | phy_number);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.ExtPageLength = mpi_reply->ExtPageLength;
	mpi_request.ExtPageType = mpi_reply->ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2SasPhyPage0_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_phy_pg1 - obtain phy page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @phy_number: phy number
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_phy_pg1(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2SasPhyPage1_t *config_page, u32 phy_number)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2SasPhyPage1_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_PHY;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_SASPHY1_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress =
	    cpu_to_le32(MPI2_SAS_PHY_PGAD_FORM_PHY_NUMBER | phy_number);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.ExtPageLength = mpi_reply->ExtPageLength;
	mpi_request.ExtPageType = mpi_reply->ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply->ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2SasPhyPage1_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_raid_volume_pg1 - obtain raid volume page 1
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: volume handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_raid_volume_pg1(struct MPT2SAS_ADAPTER *ioc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2RaidVolPage1_t *config_page, u32 form,
    u32 handle)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(config_page, 0, sizeof(Mpi2RaidVolPage1_t));
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	mpi_request.Header.PageNumber = 1;
	mpi_request.Header.PageVersion = MPI2_RAIDVOLPAGE1_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2RaidVolPage1_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_number_pds - obtain number of phys disk assigned to volume
 * @ioc: per adapter object
 * @handle: volume handle
 * @num_pds: returns pds count
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_number_pds(struct MPT2SAS_ADAPTER *ioc, u16 handle,
    u8 *num_pds)
{
	Mpi2ConfigRequest_t mpi_request;
	Mpi2RaidVolPage0_t *config_page;
	Mpi2ConfigReply_t mpi_reply;
	int r;
	struct config_request mem;
	u16 ioc_status;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	*num_pds = 0;
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_RAIDVOLPAGE0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress =
	    cpu_to_le32(MPI2_RAID_VOLUME_PGAD_FORM_HANDLE | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply.Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply.Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply.Header.PageType;
	mpi_request.Header.PageLength = mpi_reply.Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply.Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
			config_page = mem.config_page;
			*num_pds = config_page->NumPhysDisks;
		}
	}

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_raid_volume_pg0 - obtain raid volume page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: volume handle
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_raid_volume_pg0(struct MPT2SAS_ADAPTER *ioc,
    Mpi2ConfigReply_t *mpi_reply, Mpi2RaidVolPage0_t *config_page, u32 form,
    u32 handle, u16 sz)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	memset(config_page, 0, sz);
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_RAIDVOLPAGE0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | handle);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, sz, mem.config_page_sz));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_phys_disk_pg0 - obtain phys disk page 0
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_PHYSDISKNUM, PHYSDISKNUM, DEVHANDLE
 * @form_specific: specific to the form
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_phys_disk_pg0(struct MPT2SAS_ADAPTER *ioc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2RaidPhysDiskPage0_t *config_page, u32 form,
    u32 form_specific)
{
	Mpi2ConfigRequest_t mpi_request;
	int r;
	struct config_request mem;

	mutex_lock(&ioc->config_cmds.mutex);
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	memset(config_page, 0, sizeof(Mpi2RaidPhysDiskPage0_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_PHYSDISK;
	mpi_request.Header.PageNumber = 0;
	mpi_request.Header.PageVersion = MPI2_RAIDPHYSDISKPAGE0_PAGEVERSION;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress = cpu_to_le32(form | form_specific);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply->Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply->Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply->Header.PageType;
	mpi_request.Header.PageLength = mpi_reply->Header.PageLength;
	mem.config_page_sz = le16_to_cpu(mpi_reply->Header.PageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (!r)
		memcpy(config_page, mem.config_page,
		    min_t(u16, mem.config_page_sz,
		    sizeof(Mpi2RaidPhysDiskPage0_t)));

	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_volume_handle - returns volume handle for give hidden raid components
 * @ioc: per adapter object
 * @pd_handle: phys disk handle
 * @volume_handle: volume handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_volume_handle(struct MPT2SAS_ADAPTER *ioc, u16 pd_handle,
    u16 *volume_handle)
{
	Mpi2RaidConfigurationPage0_t *config_page;
	Mpi2ConfigRequest_t mpi_request;
	Mpi2ConfigReply_t mpi_reply;
	int r, i;
	struct config_request mem;
	u16 ioc_status;

	mutex_lock(&ioc->config_cmds.mutex);
	*volume_handle = 0;
	memset(&mpi_request, 0, sizeof(Mpi2ConfigRequest_t));
	mpi_request.Function = MPI2_FUNCTION_CONFIG;
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	mpi_request.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	mpi_request.ExtPageType = MPI2_CONFIG_EXTPAGETYPE_RAID_CONFIG;
	mpi_request.Header.PageVersion = MPI2_RAIDCONFIG0_PAGEVERSION;
	mpi_request.Header.PageNumber = 0;
	mpt2sas_base_build_zero_len_sge(ioc, &mpi_request.PageBufferSGE);
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	mpi_request.PageAddress =
	    cpu_to_le32(MPI2_RAID_PGAD_FORM_ACTIVE_CONFIG);
	mpi_request.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	mpi_request.Header.PageVersion = mpi_reply.Header.PageVersion;
	mpi_request.Header.PageNumber = mpi_reply.Header.PageNumber;
	mpi_request.Header.PageType = mpi_reply.Header.PageType;
	mpi_request.ExtPageLength = mpi_reply.ExtPageLength;
	mpi_request.ExtPageType = mpi_reply.ExtPageType;
	mem.config_page_sz = le16_to_cpu(mpi_reply.ExtPageLength) * 4;
	if (mem.config_page_sz > ioc->config_page_sz) {
		r = _config_alloc_config_dma_memory(ioc, &mem);
		if (r)
			goto out;
	} else {
		mem.config_page_dma = ioc->config_page_dma;
		mem.config_page = ioc->config_page;
	}
	ioc->base_add_sg_single(&mpi_request.PageBufferSGE,
	    MPT2_CONFIG_COMMON_SGLFLAGS | mem.config_page_sz,
	    mem.config_page_dma);
	r = _config_request(ioc, &mpi_request, &mpi_reply,
	    MPT2_CONFIG_PAGE_DEFAULT_TIMEOUT);
	if (r)
		goto out;

	r = -1;
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		goto done;
	config_page = mem.config_page;
	for (i = 0; i < config_page->NumElements; i++) {
		if ((config_page->ConfigElement[i].ElementFlags &
		    MPI2_RAIDCONFIG0_EFLAGS_MASK_ELEMENT_TYPE) !=
		    MPI2_RAIDCONFIG0_EFLAGS_VOL_PHYS_DISK_ELEMENT)
			continue;
		if (config_page->ConfigElement[i].PhysDiskDevHandle ==
		    pd_handle) {
			*volume_handle = le16_to_cpu(config_page->
			    ConfigElement[i].VolDevHandle);
			r = 0;
			goto done;
		}
	}

 done:
	if (mem.config_page_sz > ioc->config_page_sz)
		_config_free_config_dma_memory(ioc, &mem);

 out:
	mutex_unlock(&ioc->config_cmds.mutex);
	return r;
}

/**
 * mpt2sas_config_get_volume_wwid - returns wwid given the volume handle
 * @ioc: per adapter object
 * @volume_handle: volume handle
 * @wwid: volume wwid
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_config_get_volume_wwid(struct MPT2SAS_ADAPTER *ioc, u16 volume_handle,
    u64 *wwid)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2RaidVolPage1_t raid_vol_pg1;

	*wwid = 0;
	if (!(mpt2sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &raid_vol_pg1, MPI2_RAID_VOLUME_PGAD_FORM_HANDLE,
	    volume_handle))) {
		*wwid = le64_to_cpu(raid_vol_pg1.WWID);
		return 0;
	} else
		return -1;
}
