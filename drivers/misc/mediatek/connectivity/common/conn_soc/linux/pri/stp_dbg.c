/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/
#include <linux/kernel.h>	/* GFP_KERNEL */
#include <linux/timer.h>	/* init_timer, add_time, del_timer_sync */
#include <linux/time.h>		/* gettimeofday */
#include <linux/delay.h>
#include <linux/slab.h>		/* kzalloc */
#include <linux/sched.h>	/* task's status */
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>

#include <net/sock.h>
#include <net/netlink.h>
#include <linux/skbuff.h>
#include <net/genetlink.h>

#include <linux/zlib.h>
#include <linux/uaccess.h>
#include <linux/crc32.h>

#include "osal_typedef.h"
#include "stp_dbg.h"
/* #include "stp_btm.h" */
#include "btm_core.h"
#include "wmt_plat.h"

#define PFX_STP_DBG                      "[STPDbg]"
#define STP_DBG_LOG_LOUD                 4
#define STP_DBG_LOG_DBG                  3
#define STP_DBG_LOG_INFO                 2
#define STP_DBG_LOG_WARN                 1
#define STP_DBG_LOG_ERR                  0

unsigned int gStpDbgDbgLevel = STP_DBG_LOG_INFO;
unsigned int gStpDbgLogOut = 0;

#define STP_DBG_LOUD_FUNC(fmt, arg...) \
do { \
	if (gStpDbgDbgLevel >= STP_DBG_LOG_LOUD) \
		pr_debug(PFX_STP_DBG "%s: "  fmt, __func__ , ##arg); \
} while (0)
#define STP_DBG_DBG_FUNC(fmt, arg...) \
do { \
	if (gStpDbgDbgLevel >= STP_DBG_LOG_DBG) \
		pr_debug(PFX_STP_DBG "%s: "  fmt, __func__ , ##arg); \
} while (0)
#define STP_DBG_INFO_FUNC(fmt, arg...) \
do { \
	if (gStpDbgDbgLevel >= STP_DBG_LOG_INFO) \
		pr_debug(PFX_STP_DBG "%s: "  fmt, __func__ , ##arg); \
} while (0)
#define STP_DBG_WARN_FUNC(fmt, arg...) \
do { \
	if (gStpDbgDbgLevel >= STP_DBG_LOG_WARN) \
		pr_warn(PFX_STP_DBG "%s: "  fmt, __func__ , ##arg); \
} while (0)
#define STP_DBG_ERR_FUNC(fmt, arg...) \
do { \
	if (gStpDbgDbgLevel >= STP_DBG_LOG_ERR) \
		pr_err(PFX_STP_DBG "%s: "   fmt, __func__ , ##arg); \
} while (0)
#define STP_DBG_TRC_FUNC(f) \
do { \
	if (gStpDbgDbgLevel >= STP_DBG_LOG_DBG) \
		pr_debug(PFX_STP_DBG "<%s> <%d>\n", __func__, __LINE__); \
} while (0)

MTKSTP_DBG_T *g_stp_dbg = NULL;

#define STP_DBG_FAMILY_NAME        "STP_DBG"
#define MAX_BIND_PROCESS    (4)
#ifdef WMT_PLAT_ALPS
#define STP_DBG_AEE_EXP_API (1)
#else
#define STP_DBG_AEE_EXP_API (0)
#endif
enum {
	__STP_DBG_ATTR_INVALID,
	STP_DBG_ATTR_MSG,
	__STP_DBG_ATTR_MAX,
};
#define STP_DBG_ATTR_MAX       (__STP_DBG_ATTR_MAX - 1)

enum {
	__STP_DBG_COMMAND_INVALID,
	STP_DBG_COMMAND_BIND,
	STP_DBG_COMMAND_RESET,
	__STP_DBG_COMMAND_MAX,
};
#define MTK_WIFI_COMMAND_MAX    (__STP_DBG_COMMAND_MAX - 1)

static struct genl_family stp_dbg_gnl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = STP_DBG_FAMILY_NAME,
	.version = 1,
	.maxattr = STP_DBG_ATTR_MAX,
};

static void stp_dbg_nl_init(void);
static void stp_dbg_nl_deinit(void);
static int stp_dbg_nl_bind(struct sk_buff *skb, struct genl_info *info);
static int stp_dbg_nl_reset(struct sk_buff *skb, struct genl_info *info);

/* attribute policy */
static struct nla_policy stp_dbg_genl_policy[STP_DBG_ATTR_MAX + 1] = {
	[STP_DBG_ATTR_MSG] = {.type = NLA_NUL_STRING},
};

/* operation definition */
#if 0
static struct genl_ops stp_dbg_gnl_ops_bind = {
	.cmd = STP_DBG_COMMAND_BIND,
	.flags = 0,
	.policy = stp_dbg_genl_policy,
	.doit = stp_dbg_nl_bind,
	.dumpit = NULL,
};

static struct genl_ops stp_dbg_gnl_ops_reset = {
	.cmd = STP_DBG_COMMAND_RESET,
	.flags = 0,
	.policy = stp_dbg_genl_policy,
	.doit = stp_dbg_nl_reset,
	.dumpit = NULL,
};
#endif
static struct genl_ops stp_dbg_gnl_ops_array[] = {
	{
		.cmd = STP_DBG_COMMAND_BIND,
		.flags = 0,
		.policy = stp_dbg_genl_policy,
		.doit = stp_dbg_nl_bind,
		.dumpit = NULL,
	},
	{
		.cmd = STP_DBG_COMMAND_RESET,
		.flags = 0,
		.policy = stp_dbg_genl_policy,
		.doit = stp_dbg_nl_reset,
		.dumpit = NULL,
	},
};

#if 0
#define E2S(x) #x
static char *dmaRegsStr[] = {
	E2S(CONNSYS_CLK_GATE_STATUS),
	E2S(CONSYS_EMI_STATUS),
	E2S(SYSRAM1),
	E2S(SYSRAM2),
	E2S(SYSRAM3)
};
#endif
static unsigned int stp_dbg_seqnum;
static int num_bind_process;
static pid_t bind_pid[MAX_BIND_PROCESS];

static P_WCN_CORE_DUMP_T g_core_dump;

static P_STP_DBG_CPUPCR_T g_stp_dbg_cpupcr;

/* just show in log at present */
static P_STP_DBG_DMAREGS_T g_stp_dbg_dmaregs;

/* core_dump_timeout_handler - handler of coredump timeout
 * @ data - core dump object's pointer
 *
 * No return value
 */
static void core_dump_timeout_handler(unsigned long data)
{
	P_WCN_CORE_DUMP_T dmp = (P_WCN_CORE_DUMP_T) data;

	STP_DBG_INFO_FUNC(" start\n");

	stp_btm_notify_coredump_timeout_wq(g_stp_dbg->btm);

	STP_DBG_INFO_FUNC(" end\n");

	if (dmp)
		dmp->sm = CORE_DUMP_TIMEOUT;
}

/* wcn_core_dump_init - create core dump sys
 * @ timeout - core dump time out value
 *
 * Return object pointer if success, else NULL
 */
P_WCN_CORE_DUMP_T wcn_core_dump_init(UINT32 packet_num, UINT32 timeout)
{
#define KBYTES (1024*sizeof(char))
#define L1_BUF_SIZE (32*KBYTES)

	P_WCN_CORE_DUMP_T core_dmp = NULL;

	core_dmp = (P_WCN_CORE_DUMP_T) osal_malloc(sizeof(WCN_CORE_DUMP_T));
	if (!core_dmp) {
		STP_DBG_ERR_FUNC("alloc mem failed!\n");
		goto fail;
	}

	osal_memset(core_dmp, 0, sizeof(WCN_CORE_DUMP_T));

	core_dmp->compressor = wcn_compressor_init("core_dump_compressor", L1_BUF_SIZE, 18*packet_num*KBYTES);
	if (!core_dmp->compressor) {
		STP_DBG_ERR_FUNC("create compressor failed!\n");
		goto fail;
	}
	wcn_compressor_reset(core_dmp->compressor, 1, GZIP);

	core_dmp->dmp_timer.timeoutHandler = core_dump_timeout_handler;
	core_dmp->dmp_timer.timeroutHandlerData = (unsigned long)core_dmp;
	osal_timer_create(&core_dmp->dmp_timer);
	core_dmp->timeout = timeout;

	osal_sleepable_lock_init(&core_dmp->dmp_lock);

	core_dmp->sm = CORE_DUMP_INIT;
	STP_DBG_INFO_FUNC("create coredump object OK!\n");

	return core_dmp;

fail:
	if (core_dmp && core_dmp->compressor) {
		wcn_compressor_deinit(core_dmp->compressor);
		core_dmp->compressor = NULL;
	}
	if (core_dmp)
		osal_free(core_dmp);

	return NULL;
}
INT32 wcn_core_dump_init_gcoredump(UINT32 packet_num, UINT32 timeout)
{
	INT32 Ret = 0;

	g_core_dump = wcn_core_dump_init(packet_num, timeout);
	if (g_core_dump == NULL)
		Ret = -1;
	return Ret;
}

/* wcn_core_dump_deinit - destroy core dump object
 * @ dmp - pointer of object
 *
 * Retunr 0 if success, else error code
 */
INT32 wcn_core_dump_deinit(P_WCN_CORE_DUMP_T dmp)
{
	if (dmp && dmp->compressor) {
		wcn_compressor_deinit(dmp->compressor);
		dmp->compressor = NULL;
	}

	if (dmp) {
		osal_sleepable_lock_deinit(&dmp->dmp_lock);
		osal_timer_stop(&dmp->dmp_timer);
		osal_free(dmp);
	}

	return 0;
}

INT32 wcn_core_dump_deinit_gcoredump(VOID)
{
	wcn_core_dump_deinit(g_core_dump);
	return 0;
}

static INT32 wcn_core_dump_check_end(PUINT8 buf, INT32 len)
{
	if (strnstr(buf, "coredump end", len))
		return 1;
	else
		return 0;
}

/* wcn_core_dump_in - add a packet to compressor buffer
 * @ dmp - pointer of object
 * @ buf - input buffer
 * @ len - data length
 *
 * Retunr 0 if success; return 1 if find end string; else error code
 */
INT32 wcn_core_dump_in(P_WCN_CORE_DUMP_T dmp, PUINT8 buf, INT32 len)
{
	INT32 ret = 0;
	INT32 tmp;

#define INFO_HEAD ";SOC_CONSYS FW CORE, "

	if ((!dmp) || (!buf)) {
		STP_DBG_ERR_FUNC("invalid pointer!\n");
		return -1;
	}

	ret = osal_lock_sleepable_lock(&dmp->dmp_lock);
	if (ret) {
		STP_DBG_ERR_FUNC("--->lock dmp->dmp_lock failed, ret=%d\n", ret);
		return ret;
	}

	switch (dmp->sm) {
	case CORE_DUMP_INIT:
		wcn_compressor_reset(dmp->compressor, 1, GZIP);
		osal_timer_start(&dmp->dmp_timer, STP_CORE_DUMP_TIMEOUT);

		/* first package, copy to info buffer */
		osal_strcpy(&dmp->info[0], INFO_HEAD);

		if (NULL == (strnstr(buf, "<ASSERT>", 32))) {
			osal_memcpy(&dmp->info[osal_strlen(INFO_HEAD)], "Fw warm reset exception...",
				    osal_strlen("Fw warm reset exception..."));
			dmp->info[osal_strlen(INFO_HEAD) + osal_strlen("Fw warm reset exception...") + 1] = '\0';
		} else {
			char *pStr = buf;
			char *pDtr = NULL;

			pDtr = osal_strchr(pStr, '-');
			if (NULL != pDtr) {
				tmp = pDtr - pStr;
				osal_memcpy(&dmp->info[osal_strlen(INFO_HEAD)], buf, tmp);
				dmp->info[osal_strlen(dmp->info) + 1] = '\0';
			} else {
				tmp = STP_CORE_DUMP_INFO_SZ - osal_strlen(INFO_HEAD);
				tmp = (len > tmp) ? tmp : len;
				osal_memcpy(&dmp->info[osal_strlen(INFO_HEAD)], buf, tmp);
				dmp->info[STP_CORE_DUMP_INFO_SZ] = '\0';
			}

		}
		/* show coredump start info on UI */
		/* osal_dbg_assert_aee("MT662x f/w coredump start", "MT662x firmware coredump start"); */
#if STP_DBG_AEE_EXP_API
		aee_kernel_dal_show("SOC_CONSYS coredump start ....\n");
#endif
		/* parsing data, and check end srting */
		ret = wcn_core_dump_check_end(buf, len);
		if (ret == 1) {
			STP_DBG_INFO_FUNC("core dump end!\n");
			dmp->sm = CORE_DUMP_DONE;
			wcn_compressor_in(dmp->compressor, buf, len, 0);
		} else {
			dmp->sm = CORE_DUMP_DOING;
			wcn_compressor_in(dmp->compressor, buf, len, 0);
		}
		break;

	case CORE_DUMP_DOING:
		/* parsing data, and check end srting */
		ret = wcn_core_dump_check_end(buf, len);
		if (ret == 1) {
			STP_DBG_INFO_FUNC("core dump end!\n");
			dmp->sm = CORE_DUMP_DONE;
			wcn_compressor_in(dmp->compressor, buf, len, 0);
		} else {
			dmp->sm = CORE_DUMP_DOING;
			wcn_compressor_in(dmp->compressor, buf, len, 0);
		}
		break;

	case CORE_DUMP_DONE:
		wcn_compressor_reset(dmp->compressor, 1, GZIP);
		osal_timer_start(&dmp->dmp_timer, STP_CORE_DUMP_TIMEOUT);
		wcn_compressor_in(dmp->compressor, buf, len, 0);
		dmp->sm = CORE_DUMP_DOING;
		break;

	case CORE_DUMP_TIMEOUT:
		break;
	default:
		break;
	}

	osal_unlock_sleepable_lock(&dmp->dmp_lock);

	return ret;
}

/* wcn_core_dump_out - get compressed data from compressor buffer
 * @ dmp - pointer of object
 * @ pbuf - target buffer's pointer
 * @ len - data length
 *
 * Retunr 0 if success;  else error code
 */
INT32 wcn_core_dump_out(P_WCN_CORE_DUMP_T dmp, PUINT8 *pbuf, PINT32 plen)
{
	INT32 ret = 0;

	if ((!dmp) || (!pbuf) || (!plen)) {
		STP_DBG_ERR_FUNC("invalid pointer!\n");
		return -1;
	}

	ret = osal_lock_sleepable_lock(&dmp->dmp_lock);
	if (ret) {
		STP_DBG_ERR_FUNC("--->lock dmp->dmp_lock failed, ret=%d\n", ret);
		return ret;
	}

	ret = wcn_compressor_out(dmp->compressor, pbuf, plen);

	osal_unlock_sleepable_lock(&dmp->dmp_lock);

	return ret;
}

/* wcn_core_dump_reset - reset core dump sys
 * @ dmp - pointer of object
 * @ timeout - core dump time out value
 *
 * Retunr 0 if success, else error code
 */
INT32 wcn_core_dump_reset(P_WCN_CORE_DUMP_T dmp, UINT32 timeout)
{
	if (!dmp) {
		STP_DBG_ERR_FUNC("invalid pointer!\n");
		return -1;
	}

	dmp->sm = CORE_DUMP_INIT;
	dmp->timeout = timeout;
	osal_timer_stop(&dmp->dmp_timer);
	wcn_compressor_reset(dmp->compressor, 1, GZIP);
	osal_memset(dmp->info, 0, STP_CORE_DUMP_INFO_SZ + 1);

	wcn_core_dump_deinit(dmp);
	g_core_dump = wcn_core_dump_init(STP_CORE_DUMP_INIT_SIZE, STP_CORE_DUMP_TIMEOUT);

	return 0;
}

/* wcn_wmtd_timeout_collect_ftrace - wmtd timeout ,this func can collect SYS_FTRACE
 *
 * Retunr 0 if success
 */
#define WMTD_TIMEOUT_INFO_HEAD "Wait wmtd complation timeout ,just collect SYS_FTRACE to DB"
INT32 wcn_wmtd_timeout_collect_ftrace(void)
{
	PUINT8 pbuf;
	INT32 len;

	pbuf = "Wait wmtd complation timeout";
	len = osal_strlen("Wait wmtd complation timeout");
	osal_strcpy(&g_core_dump->info[0], WMTD_TIMEOUT_INFO_HEAD);
#ifdef WMT_PLAT_ALPS
	aed_combo_exception(NULL, 0, (const int *)pbuf, len, (const char *)g_core_dump->info);
#endif
	return 0;
}
/* wcn_psm_flag_trigger_collect_ftrace - wmtd timeout ,this func can collect SYS_FTRACE
 *
 * Retunr 0 if success
 */
#define PSM_ABNORMAL_FLAG_INFO_HEAD "Abnormal PSM flag be set ,just collect SYS_FTRACE to DB"
INT32 wcn_psm_flag_trigger_collect_ftrace(void)
{
	PUINT8 pbuf;
	INT32 len;

	pbuf = "Abnormal PSM flag be set";
	len = osal_strlen("Abnormal PSM flag be set");
	osal_strcpy(&g_core_dump->info[0], PSM_ABNORMAL_FLAG_INFO_HEAD);
#ifdef WMT_PLAT_ALPS
	aed_combo_exception(NULL, 0, (const int *)pbuf, len, (const char *)g_core_dump->info);
#endif
	return 0;
}
#if BTIF_RXD_BE_BLOCKED_DETECT
MTK_WCN_BOOL is_btif_rxd_be_blocked(void)
{
	MTK_WCN_BOOL flag = MTK_WCN_BOOL_FALSE;

	if (mtk_btif_rxd_be_blocked_flag_get())
		flag = MTK_WCN_BOOL_TRUE;
	return flag;
}
/* wcn_btif_rxd_blocked_collect_ftrace - btif rxd be blocked,this func can collect SYS_FTRACE
 *
 * Retunr 0 if success
 */
#define BTIF_RXD_BLOCKED_INFO_HEAD "Btif_rxd thread be blocked too long,just collect SYS_FTRACE to DB"
INT32 wcn_btif_rxd_blocked_collect_ftrace(void)
{
	PUINT8 pbuf;
	INT32 len;

	pbuf = "Btif_rxd thread be blocked too long";
	len = osal_strlen("Btif_rxd thread be blocked too long");
	osal_strcpy(&g_core_dump->info[0], BTIF_RXD_BLOCKED_INFO_HEAD);
#ifdef WMT_PLAT_ALPS
	aed_combo_exception(NULL, 0, (const int *)pbuf, len, (const char *)g_core_dump->info);
#endif
	return 0;
}
#endif
/* wcn_core_dump_timeout - wait for FW assert info timeout ,this func can collect SYS_FTRACE
 *
 * Retunr 0 if success
 */
#define TIMEOUT_INFO_HEAD "Trigger assert timeout ,just collect SYS_FTRACE to DB"
INT32 wcn_core_dump_timeout(void)
{
	PUINT8 pbuf;
	INT32 len;

	pbuf = "Trigger assert timeout";
	len = osal_strlen("Trigger assert timeout");
	osal_strcpy(&g_core_dump->info[0], TIMEOUT_INFO_HEAD);
#ifdef WMT_PLAT_ALPS
	aed_combo_exception(NULL, 0, (const int *)pbuf, len, (const char *)g_core_dump->info);
#endif
	return 0;
}

#define ENABLE_F_TRACE 0
/* wcn_core_dump_flush - Fulsh dump data and reset core dump sys
 *
 * Retunr 0 if success, else error code
 */
INT32 wcn_core_dump_flush(INT32 rst, MTK_WCN_BOOL coredump_is_timeout)
{
	PUINT8 pbuf = NULL;
	INT32 len = 0;

	if (!g_core_dump) {
		STP_DBG_ERR_FUNC("invalid pointer!\n");
		return -1;
	}

	wcn_core_dump_out(g_core_dump, &pbuf, &len);
	STP_DBG_INFO_FUNC("buf 0x%zx, len %d\n", (SIZE_T) pbuf, len);
#ifdef WMT_PLAT_ALPS
	/* show coredump end info on UI */
	/* osal_dbg_assert_aee("MT662x f/w coredump end", "MT662x firmware coredump ends"); */
#if STP_DBG_AEE_EXP_API
	if (coredump_is_timeout)
		aee_kernel_dal_show("++ SOC_CONSYS coredump tiemout ,pass received coredump to AEE ++\n");
	else
		aee_kernel_dal_show("++ SOC_CONSYS coredump get successfully ++\n");
	/* call AEE driver API */
#if ENABLE_F_TRACE
	aed_combo_exception_api(NULL, 0, (const int *)pbuf, len, (const char *)g_core_dump->info, DB_OPT_FTRACE);
#else
	aed_combo_exception(NULL, 0, (const int *)pbuf, len, (const char *)g_core_dump->info);
#endif

#endif

#endif  // WMT_PLAT_ALPS

	/* reset */
	wcn_core_dump_reset(g_core_dump, STP_CORE_DUMP_TIMEOUT);

	return 0;
}

static INT32 wcn_gzip_compressor(void *worker, UINT8 *in_buf, INT32 in_sz, UINT8 *out_buf, INT32 *out_sz,
				 INT32 finish)
{
	INT32 ret = 0;
	z_stream *stream = NULL;
	INT32 tmp = *out_sz;

	STP_DBG_INFO_FUNC("need to compressor :buf 0x%zx, size %d\n", (SIZE_T) in_buf, in_sz);
	STP_DBG_INFO_FUNC("before compressor,avalible buf: 0x%zx, size %d\n", (SIZE_T) out_buf, tmp);

	stream = (z_stream *) worker;
	if (!stream) {
		STP_DBG_ERR_FUNC("invalid workspace!\n");
		return -1;
	}

	if (in_sz > 0) {
#if 0
		ret = zlib_deflateReset(stream);
		if (ret != Z_OK) {
			STP_DBG_ERR_FUNC("reset failed!\n");
			return -2;
		}
#endif

		stream->next_in = in_buf;
		stream->avail_in = in_sz;
		stream->next_out = out_buf;
		stream->avail_out = tmp;

		zlib_deflate(stream, Z_FULL_FLUSH);

		if (finish) {
			while (1) {
				int val = zlib_deflate(stream, Z_FINISH);

				if (val == Z_OK)
					continue;
				else if (val == Z_STREAM_END)
					break;
				STP_DBG_ERR_FUNC("finish operation failed %d\n", val);
				return -3;
			}
		}

		*out_sz = tmp - stream->avail_out;
	}

	STP_DBG_INFO_FUNC("after compressor,avalible buf: 0x%zx, compress rate %d -> %d\n", (SIZE_T) out_buf, in_sz,
			  *out_sz);

	return ret;
}

/* wcn_compressor_init - create a compressor and do init
 * @ name - compressor's name
 * @ L1_buf_sz - L1 buffer size
 * @ L2_buf_sz - L2 buffer size
 *
 * Retunr object's pointer if success, else NULL
 */
P_WCN_COMPRESSOR_T wcn_compressor_init(PUINT8 name, INT32 L1_buf_sz, INT32 L2_buf_sz)
{
	z_stream *pstream = NULL;
	P_WCN_COMPRESSOR_T compress = NULL;

	compress = (P_WCN_COMPRESSOR_T) osal_malloc(sizeof(WCN_COMPRESSOR_T));
	if (!compress) {
		STP_DBG_ERR_FUNC("alloc compressor failed!\n");
		goto fail;
	}

	osal_memset(compress, 0, sizeof(WCN_COMPRESSOR_T));
	osal_memcpy(compress->name, name, STP_OJB_NAME_SZ);

	compress->f_compress_en = 0;
	compress->compress_type = GZIP;

	if (compress->compress_type == GZIP) {
		compress->worker = osal_malloc(sizeof(z_stream));
		if (!compress->worker) {
			STP_DBG_ERR_FUNC("alloc stream failed!\n");
			goto fail;
		}
		pstream = (z_stream *) compress->worker;

		pstream->workspace = osal_malloc(zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL));
		if (!pstream->workspace) {
			STP_DBG_ERR_FUNC("alloc workspace failed!\n");
			goto fail;
		}
		zlib_deflateInit2(pstream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS,
				  DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY);
	}

	compress->handler = wcn_gzip_compressor;
	compress->L1_buf_sz = L1_buf_sz;
	compress->L2_buf_sz = L2_buf_sz;
	compress->L1_pos = 0;
	compress->L2_pos = 0;
	compress->uncomp_size = 0;
	compress->crc32 = 0xffffffffUL;

	compress->L1_buf = osal_malloc(compress->L1_buf_sz);
	if (!compress->L1_buf) {
		STP_DBG_ERR_FUNC("alloc %d bytes for L1 buf failed!\n", compress->L1_buf_sz);
		goto fail;
	}

	compress->L2_buf = osal_malloc(compress->L2_buf_sz);
	if (!compress->L2_buf) {
		STP_DBG_ERR_FUNC("alloc %d bytes for L2 buf failed!\n", compress->L2_buf_sz);
		goto fail;
	}

	STP_DBG_INFO_FUNC("create compressor OK! L1 %d bytes, L2 %d bytes\n", L1_buf_sz, L2_buf_sz);
	return compress;

fail:
	if (compress) {
		if (compress->L2_buf) {
			osal_free(compress->L2_buf);
			compress->L2_buf = NULL;
		}

		if (compress->L1_buf) {
			osal_free(compress->L1_buf);
			compress->L1_buf = NULL;
		}

		if (compress->worker) {
			pstream = (z_stream *) compress->worker;
			if ((compress->compress_type == GZIP) && pstream->workspace) {
				zlib_deflateEnd(pstream);
				osal_free(pstream->workspace);
			}
			osal_free(compress->worker);
			compress->worker = NULL;
		}

		if (compress->worker) {
			osal_free(compress->worker);
			compress->worker = NULL;
		}

		osal_free(compress);
		compress = NULL;
	}

	STP_DBG_ERR_FUNC("init failed!\n");

	return NULL;
}

/* wcn_compressor_deinit - distroy a compressor
 * @ cprs - compressor's pointer
 *
 * Retunr 0 if success, else NULL
 */
INT32 wcn_compressor_deinit(P_WCN_COMPRESSOR_T cprs)
{
	z_stream *pstream = NULL;

	if (cprs) {
		if (cprs->L2_buf) {
			osal_free(cprs->L2_buf);
			cprs->L2_buf = NULL;
		}

		if (cprs->L1_buf) {
			osal_free(cprs->L1_buf);
			cprs->L1_buf = NULL;
		}

		if (cprs->worker) {
			pstream = (z_stream *) cprs->worker;
			if ((cprs->compress_type == GZIP) && pstream->workspace) {
				zlib_deflateEnd(pstream);
				osal_free(pstream->workspace);
			}
			osal_free(cprs->worker);
			cprs->worker = NULL;
		}

		cprs->handler = NULL;

		osal_free(cprs);
	}

	STP_DBG_INFO_FUNC("destroy OK\n");

	return 0;
}

/* wcn_compressor_in - put in a raw data, and compress L1 buffer if need
 * @ cprs - compressor's pointer
 * @ buf - raw data buffer
 * @ len - raw data length
 * @ finish - core dump finish or not, 1: finished; 0: not finish
 *
 * Retunr 0 if success, else NULL
 */
INT32 wcn_compressor_in(P_WCN_COMPRESSOR_T cprs, PUINT8 buf, INT32 len, INT32 finish)
{
	INT32 tmp_len = 0;
	INT32 ret = 0;

	if (!cprs) {
		STP_DBG_ERR_FUNC("invalid para!\n");
		return -1;
	}

	cprs->uncomp_size += len;

	/* check L1 buf valid space */
	if (len > (cprs->L1_buf_sz - cprs->L1_pos)) {
		STP_DBG_INFO_FUNC("L1 buffer full\n");

		if (cprs->f_compress_en && cprs->handler) {
			/* need compress */
			/* compress L1 buffer, and put result to L2 buffer */
			tmp_len = cprs->L2_buf_sz - cprs->L2_pos;
			ret =
			    cprs->handler(cprs->worker, cprs->L1_buf, cprs->L1_pos, &cprs->L2_buf[cprs->L2_pos],
					  &tmp_len, finish);
			if (!ret) {
				cprs->crc32 = (crc32(cprs->crc32, cprs->L1_buf, cprs->L1_pos));
				cprs->L2_pos += tmp_len;
				if (cprs->L2_pos > cprs->L2_buf_sz)
					STP_DBG_ERR_FUNC("coredump size too large(%d), L2 buf overflow\n",
					cprs->L2_pos);

				if (finish) {
					/* Add 8 byte suffix
					   ===
					   32 bits UNCOMPRESS SIZE
					   32 bits CRC
					 */
					*(uint32_t *) (&cprs->L2_buf[cprs->L2_pos]) = (cprs->crc32 ^ 0xffffffffUL);
					*(uint32_t *) (&cprs->L2_buf[cprs->L2_pos + 4]) = cprs->uncomp_size;
					cprs->L2_pos += 8;
				}
				STP_DBG_INFO_FUNC("compress OK!\n");
			} else
				STP_DBG_ERR_FUNC("compress error!\n");
		} else {
			/* no need compress */
			/* Flush L1 buffer to L2 buffer */
			STP_DBG_INFO_FUNC("No need do compress, Put to L2 buf\n");

			tmp_len = cprs->L2_buf_sz - cprs->L2_pos;
			tmp_len = (cprs->L1_pos > tmp_len) ? tmp_len : cprs->L1_pos;
			osal_memcpy(&cprs->L2_buf[cprs->L2_pos], cprs->L1_buf, tmp_len);
			cprs->L2_pos += tmp_len;
		}

		/* reset L1 buf pos */
		cprs->L1_pos = 0;

		/* put curren data to L1 buf */
		if (len > cprs->L1_buf_sz) {
			STP_DBG_ERR_FUNC("len=%d, too long err!\n", len);
		} else {
			STP_DBG_INFO_FUNC("L1 Flushed, and Put %d bytes to L1 buf\n", len);
			osal_memcpy(&cprs->L1_buf[cprs->L1_pos], buf, len);
			cprs->L1_pos += len;
		}
	} else {
		/* put to L1 buffer */
		STP_DBG_INFO_FUNC("Put %d bytes to L1 buf\n", len);

		osal_memcpy(&cprs->L1_buf[cprs->L1_pos], buf, len);
		cprs->L1_pos += len;
	}

	return ret;
}

/* wcn_compressor_out - get the result data from L2 buffer
 * @ cprs - compressor's pointer
 * @ pbuf - point to L2 buffer
 * @ plen - out len
 *
 * Retunr 0 if success, else NULL
 */
INT32 wcn_compressor_out(P_WCN_COMPRESSOR_T cprs, PUINT8 *pbuf, PINT32 plen)
{
	INT32 ret = 0;
	INT32 tmp_len = 0;

	if ((!cprs) || (!pbuf) || (!plen)) {
		STP_DBG_ERR_FUNC("invalid para!\n");
		return -1;
	}
	/* check if there's L1 data need flush to L2 buffer */
	if (cprs->L1_pos > 0) {
		tmp_len = cprs->L2_buf_sz - cprs->L2_pos;

		if (cprs->f_compress_en && cprs->handler) {
			/* need compress */
			ret =
			    cprs->handler(cprs->worker, cprs->L1_buf, cprs->L1_pos, &cprs->L2_buf[cprs->L2_pos],
					  &tmp_len, 1);

			if (!ret) {
				cprs->crc32 = (crc32(cprs->crc32, cprs->L1_buf, cprs->L1_pos));
				cprs->L2_pos += tmp_len;

				/* Add 8 byte suffix
				   ===
				   32 bits UNCOMPRESS SIZE
				   32 bits CRC
				 */
				*(uint32_t *) (&cprs->L2_buf[cprs->L2_pos]) = (cprs->crc32 ^ 0xffffffffUL);
				*(uint32_t *) (&cprs->L2_buf[cprs->L2_pos + 4]) = cprs->uncomp_size;
				cprs->L2_pos += 8;

				STP_DBG_INFO_FUNC("compress OK!\n");
			} else {
				STP_DBG_ERR_FUNC("compress error!\n");
			}
		} else {
			/* no need compress */
			tmp_len = (cprs->L1_pos > tmp_len) ? tmp_len : cprs->L1_pos;
			osal_memcpy(&cprs->L2_buf[cprs->L2_pos], cprs->L1_buf, tmp_len);
			cprs->L2_pos += tmp_len;
		}

		cprs->L1_pos = 0;
	}

	*pbuf = cprs->L2_buf;
	*plen = cprs->L2_pos;

	STP_DBG_INFO_FUNC("0x%zx, len %d\n", (SIZE_T)*pbuf, *plen);

	return 0;
}

/* wcn_compressor_reset - reset compressor
 * @ cprs - compressor's pointer
 * @ enable - enable/disable compress
 * @ type - compress algorithm
 *
 * Retunr 0 if success, else NULL
 */
INT32 wcn_compressor_reset(P_WCN_COMPRESSOR_T cprs, UINT8 enable, WCN_COMPRESS_ALG_T type)
{
	if (!cprs) {
		STP_DBG_ERR_FUNC("invalid para!\n");
		return -1;
	}

	cprs->f_compress_en = enable;
	/* cprs->f_compress_en = 0; // disable compress for test */
	cprs->compress_type = type;
	cprs->L1_pos = 0;
	cprs->L2_pos = 0;
	cprs->uncomp_size = 0;
	cprs->crc32 = 0xffffffffUL;

	/* zlib_deflateEnd((z_stream*)cprs->worker); */

	STP_DBG_INFO_FUNC("OK! compress algorithm %d\n", type);

	return 0;
}

static void stp_dbg_dump_data(unsigned char *pBuf, char *title, int len)
{
	int k = 0;

	pr_debug(" %s-len:%d\n", title, len);

	for (k = 0; k < len; k++) {
		if (k % 16 == 0 && k != 0)
			pr_cont("\n    ");
		pr_cont("0x%02x ", pBuf[k]);
	}
	pr_debug("--end\n");
}

static int _stp_dbg_enable(MTKSTP_DBG_T *stp_dbg)
{

	unsigned long flags;

	spin_lock_irqsave(&(stp_dbg->logsys->lock), flags);
	stp_dbg->pkt_trace_no = 0;
	stp_dbg->is_enable = 1;
	spin_unlock_irqrestore(&(stp_dbg->logsys->lock), flags);

	return 0;
}

static int _stp_dbg_disable(MTKSTP_DBG_T *stp_dbg)
{

	unsigned long flags;

	spin_lock_irqsave(&(stp_dbg->logsys->lock), flags);
	stp_dbg->pkt_trace_no = 0;
	memset(stp_dbg->logsys, 0, sizeof(MTKSTP_LOG_SYS_T));
	stp_dbg->is_enable = 0;
	spin_unlock_irqrestore(&(stp_dbg->logsys->lock), flags);

	return 0;
}

static int _stp_dbg_dmp_in(MTKSTP_DBG_T *stp_dbg, char *buf, int len)
{
	unsigned long flags;
	STP_DBG_HDR_T *pHdr = NULL;
	char *pBuf = NULL;
	unsigned int length = 0;
	unsigned int internalFlag = stp_dbg->logsys->size < STP_DBG_LOG_ENTRY_NUM;
	/* #ifdef CONFIG_LOG_STP_INTERNAL */
	/* Here we record log in this circle buffer, if buffer is full ,
	select to overlap earlier log, logic should be okay */
	internalFlag = 1;
	/* #endif */
	spin_lock_irqsave(&(stp_dbg->logsys->lock), flags);

	if (internalFlag) {
		stp_dbg->logsys->queue[stp_dbg->logsys->in].id = 0;
		stp_dbg->logsys->queue[stp_dbg->logsys->in].len = len;
		memset(&(stp_dbg->logsys->queue[stp_dbg->logsys->in].buffer[0]),
		       0, ((len >= STP_DBG_LOG_ENTRY_SZ) ? (STP_DBG_LOG_ENTRY_SZ) : (len)));
		memcpy(&(stp_dbg->logsys->queue[stp_dbg->logsys->in].buffer[0]),
		       buf, ((len >= STP_DBG_LOG_ENTRY_SZ) ? (STP_DBG_LOG_ENTRY_SZ) : (len)));

		stp_dbg->logsys->size++;
		stp_dbg->logsys->size =
		    (stp_dbg->logsys->size > STP_DBG_LOG_ENTRY_NUM) ? STP_DBG_LOG_ENTRY_NUM : stp_dbg->logsys->size;

		if (0 != gStpDbgLogOut) {
			pHdr = (STP_DBG_HDR_T *) &(stp_dbg->logsys->queue[stp_dbg->logsys->in].buffer[0]);
			pBuf = (char *)&(stp_dbg->logsys->queue[stp_dbg->logsys->in].buffer[0]) + sizeof(STP_DBG_HDR_T);
			length = stp_dbg->logsys->queue[stp_dbg->logsys->in].len - sizeof(STP_DBG_HDR_T);
			pr_debug("STP-DBG:%d.%ds, %s:pT%sn(%d)l(%d)s(%d)a(%d)\n",
			       pHdr->sec,
			       pHdr->usec,
			       pHdr->dir == PKT_DIR_TX ? "Tx" : "Rx",
			       gStpDbgType[pHdr->type], pHdr->no, pHdr->len, pHdr->seq, pHdr->ack);
			if (0 < length)
				stp_dbg_dump_data(pBuf, pHdr->dir == PKT_DIR_TX ? "Tx" : "Rx", length);
		}
		stp_dbg->logsys->in =
		    (stp_dbg->logsys->in >= (STP_DBG_LOG_ENTRY_NUM - 1)) ? (0) : (stp_dbg->logsys->in + 1);
		STP_DBG_DBG_FUNC("logsys size = %d, in = %d\n", stp_dbg->logsys->size, stp_dbg->logsys->in);
	} else {
		STP_DBG_WARN_FUNC("logsys FULL!\n");
	}

	spin_unlock_irqrestore(&(stp_dbg->logsys->lock), flags);

	return 0;
}

int stp_gdb_notify_btm_dmp_wq(MTKSTP_DBG_T *stp_dbg)
{
	int retval = 0;
/* #ifndef CONFIG_LOG_STP_INTERNAL */

	if (stp_dbg->btm != NULL)
		retval += stp_btm_notify_wmt_dmp_wq((MTKSTP_BTM_T *) stp_dbg->btm);
/* #endif */

	return retval;
}

int stp_dbg_log_ctrl(unsigned int on)
{
	if (on != 0) {
		gStpDbgLogOut = 1;
		pr_debug("STP-DBG: enable pkt log dump out.\n");
	} else {
		gStpDbgLogOut = 0;
		pr_debug("STP-DBG: disable pkt log dump out.\n");
	}
	return 0;
}

int stp_dbg_dmp_in(MTKSTP_DBG_T *stp_dbg, char *buf, int len)
{
	return _stp_dbg_dmp_in(stp_dbg, buf, len);
}

int stp_dbg_dmp_printk(MTKSTP_DBG_T *stp_dbg)
{
#define MAX_DMP_NUM 80
	unsigned long flags;
	char *pBuf = NULL;
	int len = 0;
	STP_DBG_HDR_T *pHdr = NULL;
	UINT32 dumpSize = 0;
	UINT32 inIndex = 0;
	UINT32 outIndex = 0;

	spin_lock_irqsave(&(stp_dbg->logsys->lock), flags);
	/* Not to dequeue from loging system */
	inIndex = stp_dbg->logsys->in;
	dumpSize = stp_dbg->logsys->size;
	if (STP_DBG_LOG_ENTRY_NUM == dumpSize)
		outIndex = inIndex;
	else
		outIndex = ((inIndex + STP_DBG_LOG_ENTRY_NUM) - dumpSize) % STP_DBG_LOG_ENTRY_NUM;

	if (dumpSize > MAX_DMP_NUM) {

		outIndex += (dumpSize - MAX_DMP_NUM);
		outIndex %= STP_DBG_LOG_ENTRY_NUM;
		dumpSize = MAX_DMP_NUM;

	}
	STP_DBG_INFO_FUNC("loged packet size = %d, in(%d), out(%d)\n", dumpSize, inIndex, outIndex);
	while (dumpSize > 0) {
		pHdr = (STP_DBG_HDR_T *) &(stp_dbg->logsys->queue[outIndex].buffer[0]);
		pBuf = &(stp_dbg->logsys->queue[outIndex].buffer[0]) + sizeof(STP_DBG_HDR_T);
		len = stp_dbg->logsys->queue[outIndex].len - sizeof(STP_DBG_HDR_T);
		len = len > STP_PKT_SZ ? STP_PKT_SZ : len;
		pr_debug("STP-DBG:%d.%ds, %s:pT%sn(%d)l(%d)s(%d)a(%d)\n",
		       pHdr->sec,
		       pHdr->usec,
		       pHdr->dir == PKT_DIR_TX ? "Tx" : "Rx",
		       gStpDbgType[pHdr->type], pHdr->no, pHdr->len, pHdr->seq, pHdr->ack);

		if (0 < len)
			stp_dbg_dump_data(pBuf, pHdr->dir == PKT_DIR_TX ? "Tx" : "Rx", len);
		outIndex = (outIndex >= (STP_DBG_LOG_ENTRY_NUM - 1)) ? (0) : (outIndex + 1);
		dumpSize--;

	}

	spin_unlock_irqrestore(&(stp_dbg->logsys->lock), flags);

	return 0;
}

int stp_dbg_dmp_out_ex(char *buf, int *len)
{
	return stp_dbg_dmp_out(g_stp_dbg, buf, len);
}

int stp_dbg_dmp_out(MTKSTP_DBG_T *stp_dbg, char *buf, int *len)
{

	unsigned long flags;
	int remaining = 0;
	*len = 0;
	spin_lock_irqsave(&(stp_dbg->logsys->lock), flags);

	if (stp_dbg->logsys->size > 0) {
		memcpy(buf, &(stp_dbg->logsys->queue[stp_dbg->logsys->out].buffer[0]),
		       stp_dbg->logsys->queue[stp_dbg->logsys->out].len);

		(*len) = stp_dbg->logsys->queue[stp_dbg->logsys->out].len;
		stp_dbg->logsys->out =
		    (stp_dbg->logsys->out >= (STP_DBG_LOG_ENTRY_NUM - 1)) ? (0) : (stp_dbg->logsys->out + 1);
		stp_dbg->logsys->size--;

		STP_DBG_DBG_FUNC("logsys size = %d, out = %d\n", stp_dbg->logsys->size, stp_dbg->logsys->out);
	} else {
		STP_DBG_LOUD_FUNC("logsys EMPTY!\n");
	}

	remaining = (stp_dbg->logsys->size == 0) ? (0) : (1);

	spin_unlock_irqrestore(&(stp_dbg->logsys->lock), flags);

	return remaining;
}

static int stp_dbg_fill_hdr(struct stp_dbg_pkt_hdr *hdr, int type, int ack, int seq, int crc, int dir, int len,
			    int dbg_type)
{

	struct timeval now;

	if (!hdr) {
		STP_DBG_ERR_FUNC("function invalid\n");
		return -EINVAL;
	}
	do_gettimeofday(&now);
	hdr->dbg_type = dbg_type;
	hdr->ack = ack;
	hdr->seq = seq;
	hdr->sec = now.tv_sec;
	hdr->usec = now.tv_usec;
	hdr->crc = crc;
	hdr->dir = dir;	/* rx */
	hdr->dmy = 0xffffffff;
	hdr->len = len;
	hdr->type = type;
	return 0;

}

static int stp_dbg_add_pkt(MTKSTP_DBG_T *stp_dbg, struct stp_dbg_pkt_hdr *hdr, const unsigned char *body)
{
	/* fix the frame size large issues. */
	static struct stp_dbg_pkt stp_pkt;
	uint32_t hdr_sz = sizeof(struct stp_dbg_pkt_hdr);
	uint32_t body_sz = 0;

	BUG_ON(!stp_dbg);

	if (hdr->dbg_type == STP_DBG_PKT)
		body_sz = (hdr->len <= STP_PKT_SZ) ? (hdr->len) : (STP_PKT_SZ);
	else
		body_sz = (hdr->len <= STP_DMP_SZ) ? (hdr->len) : (STP_DMP_SZ);

	hdr->no = stp_dbg->pkt_trace_no++;
	memcpy((uint8_t *) &stp_pkt.hdr, (uint8_t *) hdr, hdr_sz);
	if (body != NULL)
		memcpy((uint8_t *) &stp_pkt.raw[0], body, body_sz);

	_stp_dbg_dmp_in(stp_dbg, (char *)&stp_pkt, hdr_sz + body_sz);
	/* Only FW DMP MSG should inform BTM-CORE to dump packet to native process */
	if (hdr->dbg_type == STP_DBG_FW_DMP)
		stp_gdb_notify_btm_dmp_wq(stp_dbg);

	return 0;
}

int stp_dbg_log_pkt(MTKSTP_DBG_T *stp_dbg, int dbg_type,
		    int type, int ack_no, int seq_no, int crc, int dir, int len, const unsigned char *body)
{

	struct stp_dbg_pkt_hdr hdr;

	if (stp_dbg->is_enable == 0) {
		/*dbg is disable,and not to log */
	} else {
		hdr.no = 0;
		hdr.chs = 0;
		stp_dbg_fill_hdr(&hdr,
				 (int)type, (int)ack_no, (int)seq_no, (int)crc, (int)dir, (int)len, (int)dbg_type);

		stp_dbg_add_pkt(stp_dbg, &hdr, body);
	}

	return 0;
}

int stp_dbg_enable(MTKSTP_DBG_T *stp_dbg)
{
	return _stp_dbg_enable(stp_dbg);
}

int stp_dbg_disable(MTKSTP_DBG_T *stp_dbg)
{
	return _stp_dbg_disable(stp_dbg);
}

static void stp_dbg_nl_init(void)
{
#if 0
	if (genl_register_family(&stp_dbg_gnl_family) != 0) {
		STP_DBG_ERR_FUNC("%s(): GE_NELINK family registration fail\n", __func__);
	} else {
		if (genl_register_ops(&stp_dbg_gnl_family, &stp_dbg_gnl_ops_bind) != 0)
			STP_DBG_ERR_FUNC("%s(): BIND operation registration fail\n", __func__);

		if (genl_register_ops(&stp_dbg_gnl_family, &stp_dbg_gnl_ops_reset) != 0)
			STP_DBG_ERR_FUNC("%s(): RESET operation registration fail\n", __func__);

	}
#endif
	if (genl_register_family_with_ops(&stp_dbg_gnl_family, stp_dbg_gnl_ops_array) != 0)
		STP_DBG_ERR_FUNC("%s(): GE_NELINK family registration fail\n", __func__);
}

static void stp_dbg_nl_deinit(void)
{
	genl_unregister_family(&stp_dbg_gnl_family);
}

static int stp_dbg_nl_bind(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *na;
	char *mydata;

	if (info == NULL)
		goto out;

	STP_DBG_INFO_FUNC("%s():->\n", __func__);

	na = info->attrs[STP_DBG_ATTR_MSG];

	if (na)
		mydata = (char *)nla_data(na);

	if (num_bind_process < MAX_BIND_PROCESS) {
		bind_pid[num_bind_process] = info->snd_portid;
		num_bind_process++;
		STP_DBG_INFO_FUNC("%s():-> pid  = %d\n", __func__, info->snd_portid);
	} else {
		STP_DBG_ERR_FUNC("%s(): exceeding binding limit %d\n", __func__, MAX_BIND_PROCESS);
	}

out:
	return 0;
}

static int stp_dbg_nl_reset(struct sk_buff *skb, struct genl_info *info)
{
	STP_DBG_ERR_FUNC("%s(): should not be invoked\n", __func__);

	return 0;
}

INT8 stp_dbg_nl_send(PINT8 aucMsg, UINT8 cmd, INT32 len)
{
	struct sk_buff *skb = NULL;
	void *msg_head = NULL;
	int rc = -1;
	int i;

	if (num_bind_process == 0) {
		/* no listening process */
		STP_DBG_ERR_FUNC("%s(): the process is not invoked\n", __func__);
		return 0;
	}

	for (i = 0; i < num_bind_process; i++) {
		skb = genlmsg_new(2048, GFP_KERNEL);

		if (skb) {
			msg_head = genlmsg_put(skb, 0, stp_dbg_seqnum++, &stp_dbg_gnl_family, 0, cmd);
			if (msg_head == NULL) {
				nlmsg_free(skb);
				STP_DBG_ERR_FUNC("%s(): genlmsg_put fail...\n", __func__);
				return -1;
			}

			rc = nla_put(skb, STP_DBG_ATTR_MSG, len, aucMsg);
			if (rc != 0) {
				nlmsg_free(skb);
				STP_DBG_ERR_FUNC("%s(): nla_put_string fail...%d\n", __func__, rc);
				return -1;
			}

			/* finalize the message */
			genlmsg_end(skb, msg_head);

			/* sending message */
			rc = genlmsg_unicast(&init_net, skb, bind_pid[i]);
			if (rc != 0) {
				STP_DBG_ERR_FUNC("%s(): genlmsg_unicast fail...\n", __func__);
				return -1;
			}
		} else {
			STP_DBG_ERR_FUNC("%s(): genlmsg_new fail...\n", __func__);
			return -1;
		}
	}

	return 0;
}

INT32 stp_dbg_aee_send(unsigned char *aucMsg, INT32 len, INT32 cmd)
{
	INT32 ret = 0;

	/* buffered to compressor */
	ret = wcn_core_dump_in(g_core_dump, aucMsg, len);
	if (ret == 1)
		wcn_core_dump_flush(0, MTK_WCN_BOOL_FALSE);

	return ret;
}

UINT8 *_stp_dbg_id_to_task(UINT32 id)
{
	UINT8 *taskStr[] = {
		"Task_WMT",
		"Task_BT",
		"Task_Wifi",
		"Task_Tst",
		"Task_FM",
		"Task_Idle",
		"Task_DrvStp",
		"Task_DrvBtif",
		"Task_NatBt"
	};
	return taskStr[id];
}

INT32 _stp_dbg_parser_assert_str(PINT8 str, ENUM_ASSERT_INFO_PARSER_TYPE type)
{
	char *pStr = NULL;
	char *pDtr = NULL;
	char *pTemp = NULL;
	char *pTemp2 = NULL;
	char tempBuf[64] = { 0 };
	UINT32 len = 0;
	long res;
	INT32 ret;

	PUINT8 parser_sub_string[] = {
		"<ASSERT> ",
		"id=",
		"isr=",
		"irq=",
		"rc="
	};

	if (!str) {
		STP_DBG_ERR_FUNC("NULL string source\n");
		return -1;
	}

	if (!g_stp_dbg_cpupcr) {
		STP_DBG_ERR_FUNC("NULL pointer\n");
		return -2;
	}

	pStr = str;
	STP_DBG_DBG_FUNC("source infor:%s\n", pStr);
	switch (type) {
	case STP_DBG_ASSERT_INFO:
		pDtr = osal_strstr(pStr, parser_sub_string[type]);
		if (NULL != pDtr) {
			pDtr += osal_strlen(parser_sub_string[type]);
			pTemp = osal_strchr(pDtr, ' ');
		} else {
			STP_DBG_ERR_FUNC("parser str is NULL,substring(%s)\n", parser_sub_string[type]);
			return -3;
		}
		len = pTemp - pDtr;
		osal_memcpy(&g_stp_dbg_cpupcr->assert_info[0], "assert@", osal_strlen("assert@"));
		osal_memcpy(&g_stp_dbg_cpupcr->assert_info[osal_strlen("assert@")], pDtr, len);
		g_stp_dbg_cpupcr->assert_info[osal_strlen("assert@") + len] = '_';

		pTemp = osal_strchr(pDtr, '#');
		pTemp += 1;

		pTemp2 = osal_strchr(pTemp, ' ');
		osal_memcpy(&g_stp_dbg_cpupcr->assert_info[osal_strlen("assert@") + len + 1], pTemp, pTemp2 - pTemp);
		g_stp_dbg_cpupcr->assert_info[osal_strlen("assert@") + len + 1 + pTemp2 - pTemp] = '\0';
		STP_DBG_INFO_FUNC("assert info:%s\n", &g_stp_dbg_cpupcr->assert_info[0]);
		break;
	case STP_DBG_FW_TASK_ID:
		pDtr = osal_strstr(pStr, parser_sub_string[type]);
		if (NULL != pDtr) {
			pDtr += osal_strlen(parser_sub_string[type]);
			pTemp = osal_strchr(pDtr, ' ');
		} else {
			STP_DBG_ERR_FUNC("parser str is NULL,substring(%s)\n", parser_sub_string[type]);
			return -3;
		}
		len = pTemp - pDtr;
		osal_memcpy(&tempBuf[0], pDtr, len);
		tempBuf[len] = '\0';
		ret = osal_strtol(tempBuf, 16, &res);
		if (ret) {
			STP_DBG_ERR_FUNC("get fw task id fail(%d)\n", ret);
			return -4;
		}
		g_stp_dbg_cpupcr->fwTaskId = (UINT32)res;

		STP_DBG_INFO_FUNC("fw task id :%x\n", (UINT32)res);
		break;
	case STP_DBG_FW_ISR:
		pDtr = osal_strstr(pStr, parser_sub_string[type]);
		if (NULL != pDtr) {
			pDtr += osal_strlen(parser_sub_string[type]);
			pTemp = osal_strchr(pDtr, ',');
		} else {
			STP_DBG_ERR_FUNC("parser str is NULL,substring(%s)\n", parser_sub_string[type]);
			return -3;
		}
		len = pTemp - pDtr;
		osal_memcpy(&tempBuf[0], pDtr, len);
		tempBuf[len] = '\0';
		ret = osal_strtol(tempBuf, 16, &res);
		if (ret) {
			STP_DBG_ERR_FUNC("get fw isr id fail(%d)\n", ret);
			return -4;
		}
		g_stp_dbg_cpupcr->fwIsr = (UINT32)res;

		STP_DBG_INFO_FUNC("fw isr str:%x\n", (UINT32)res);
		break;
	case STP_DBG_FW_IRQ:
		pDtr = osal_strstr(pStr, parser_sub_string[type]);
		if (NULL != pDtr) {
			pDtr += osal_strlen(parser_sub_string[type]);
			pTemp = osal_strchr(pDtr, ',');
		} else {
			STP_DBG_ERR_FUNC("parser str is NULL,substring(%s)\n", parser_sub_string[type]);
			return -3;
		}
		len = pTemp - pDtr;
		osal_memcpy(&tempBuf[0], pDtr, len);
		tempBuf[len] = '\0';
		ret = osal_strtol(tempBuf, 16, &res);
		if (ret) {
			STP_DBG_ERR_FUNC("get fw irq id fail(%d)\n", ret);
			return -4;
		}
		g_stp_dbg_cpupcr->fwRrq = (UINT32)res;

		STP_DBG_INFO_FUNC("fw irq value:%x\n", (UINT32)res);
		break;
	case STP_DBG_ASSERT_TYPE:
		pDtr = osal_strstr(pStr, parser_sub_string[type]);
		if (NULL != pDtr) {
			pDtr += osal_strlen(parser_sub_string[type]);
			pTemp = osal_strchr(pDtr, ',');
		} else {
			STP_DBG_ERR_FUNC("parser str is NULL,substring(%s)\n", parser_sub_string[type]);
			return -3;
		}
		len = pTemp - pDtr;
		osal_memcpy(&tempBuf[0], pDtr, len);
		tempBuf[len] = '\0';

		if (0 == osal_memcmp(tempBuf, "*", len))
			osal_memcpy(&g_stp_dbg_cpupcr->assert_type[0], "general assert", osal_strlen("general assert"));
		if (0 == osal_memcmp(tempBuf, "Watch Dog Timeout", len))
			osal_memcpy(&g_stp_dbg_cpupcr->assert_type[0], "wdt", osal_strlen("wdt"));
		if (0 == osal_memcmp(tempBuf, "RB_FULL", osal_strlen("RB_FULL"))) {
			osal_memcpy(&g_stp_dbg_cpupcr->assert_type[0], tempBuf, len);

			pDtr = osal_strstr(&g_stp_dbg_cpupcr->assert_type[0], "RB_FULL(");
			if (NULL != pDtr) {
				pDtr += osal_strlen("RB_FULL(");
				pTemp = osal_strchr(pDtr, ')');
			} else {
				STP_DBG_ERR_FUNC("parser str is NULL,substring(RB_FULL()\n");
				return -4;
			}
			len = pTemp - pDtr;
			osal_memcpy(&tempBuf[0], pDtr, len);
			tempBuf[len] = '\0';
			ret = osal_strtol(tempBuf, 16, &res);
			if (ret) {
				STP_DBG_ERR_FUNC("get fw task id fail(%d)\n", ret);
				return -5;
			}
			g_stp_dbg_cpupcr->fwTaskId = (UINT32)res;

			STP_DBG_INFO_FUNC("update fw task id :%x\n", (UINT32)res);
		}

		STP_DBG_INFO_FUNC("fw asert type:%s\n", g_stp_dbg_cpupcr->assert_type);
		break;
	default:
		STP_DBG_ERR_FUNC("unknown parser type\n");
		break;
	}

	return 0;
}

P_STP_DBG_CPUPCR_T stp_dbg_cpupcr_init(VOID)
{
	P_STP_DBG_CPUPCR_T pSdCpupcr = NULL;

	pSdCpupcr = (P_STP_DBG_CPUPCR_T) osal_malloc(osal_sizeof(STP_DBG_CPUPCR_T));
	if (!pSdCpupcr) {
		STP_DBG_ERR_FUNC("stp dbg cpupcr allocate memory fail!\n");
		return NULL;
	}

	osal_memset(pSdCpupcr, 0, osal_sizeof(STP_DBG_CPUPCR_T));

	osal_sleepable_lock_init(&pSdCpupcr->lock);

	return pSdCpupcr;
}

P_STP_DBG_DMAREGS_T stp_dbg_dmaregs_init(VOID)
{
	P_STP_DBG_DMAREGS_T pDmaRegs = NULL;

	pDmaRegs = (P_STP_DBG_DMAREGS_T) osal_malloc(osal_sizeof(STP_DBG_DMAREGS_T));
	if (!pDmaRegs) {
		STP_DBG_ERR_FUNC("stp dbg dmareg allocate memory fail!\n");
		return NULL;
	}

	osal_memset(pDmaRegs, 0, osal_sizeof(STP_DBG_DMAREGS_T));

	osal_sleepable_lock_init(&pDmaRegs->lock);

	return pDmaRegs;
}

VOID stp_dbg_cpupcr_deinit(P_STP_DBG_CPUPCR_T pCpupcr)
{
	if (pCpupcr) {
		osal_sleepable_lock_deinit(&pCpupcr->lock);
		osal_free(pCpupcr);
		pCpupcr = NULL;
	}
}

VOID stp_dbg_dmaregs_deinit(P_STP_DBG_DMAREGS_T pDmaRegs)
{
	if (pDmaRegs) {
		osal_sleepable_lock_deinit(&pDmaRegs->lock);
		osal_free(pDmaRegs);
		pDmaRegs = NULL;
	}
}

INT32 stp_dbg_poll_cpupcr(UINT32 times, UINT32 sleep, UINT32 cmd)
{
	INT32 i = 0;

	if (!g_stp_dbg_cpupcr) {
		STP_DBG_ERR_FUNC("NULL reference pointer\n");
		return -1;
	}

	if (!cmd) {
		if (g_stp_dbg_cpupcr->count + times > STP_DBG_CPUPCR_NUM)
			times = STP_DBG_CPUPCR_NUM - g_stp_dbg_cpupcr->count;

		osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);

		for (i = 0; i < times; i++) {
			STP_DBG_INFO_FUNC("i:%d,cpupcr:%08x\n", i, wmt_plat_read_cpupcr());
			/* osal_memcpy(
			* &g_stp_dbg_cpupcr->buffer[i],
			* (UINT8*)(CONSYS_REG_READ(CONSYS_CPUPCR_REG)),
			* osal_sizeof(UINT32));
			*/
			g_stp_dbg_cpupcr->buffer[g_stp_dbg_cpupcr->count + i] = wmt_plat_read_cpupcr();
			osal_sleep_ms(sleep);
		}
		g_stp_dbg_cpupcr->count += times;

		osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
	} else {
		STP_DBG_INFO_FUNC("stp-dbg: for proc test polling cpupcr\n");
		if (times > STP_DBG_CPUPCR_NUM)
			times = STP_DBG_CPUPCR_NUM;

		osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
		g_stp_dbg_cpupcr->count = 0;
		for (i = 0; i < times; i++) {
			STP_DBG_INFO_FUNC("i:%d,cpupcr:%08x\n", i, wmt_plat_read_cpupcr());
			/* osal_memcpy(
			* &g_stp_dbg_cpupcr->buffer[i],
			* (UINT8*)(CONSYS_REG_READ(CONSYS_CPUPCR_REG)),
			* osal_sizeof(UINT32));
			*/
			g_stp_dbg_cpupcr->buffer[i] = wmt_plat_read_cpupcr();
			osal_sleep_ms(sleep);
		}
		g_stp_dbg_cpupcr->count = times;

		osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);

	}
	return 0;
}

INT32 stp_dbg_poll_dmaregs(UINT32 times, UINT32 sleep)
{
#if 0
	INT32 i = 0;

	if (!g_stp_dbg_dmaregs) {
		STP_DBG_ERR_FUNC("NULL reference pointer\n");
		return -1;
	}

	osal_lock_sleepable_lock(&g_stp_dbg_dmaregs->lock);

	if (g_stp_dbg_dmaregs->count + times > STP_DBG_DMAREGS_NUM) {
		if (g_stp_dbg_dmaregs->count > STP_DBG_DMAREGS_NUM) {
			STP_DBG_ERR_FUNC("g_stp_dbg_dmaregs->count:%d must less than STP_DBG_DMAREGS_NUM:%d\n",
				g_stp_dbg_dmaregs->count, STP_DBG_DMAREGS_NUM);
			g_stp_dbg_dmaregs->count = 0;
			STP_DBG_ERR_FUNC("g_stp_dbg_dmaregs->count be set default value 0\n");
		}
		times = STP_DBG_DMAREGS_NUM - g_stp_dbg_dmaregs->count;
	}
	if (times > STP_DBG_DMAREGS_NUM) {
		STP_DBG_ERR_FUNC("times overflow, set default value:0\n");
		times = 0;
	}
	STP_DBG_WARN_FUNC("---------Now Polling DMA relative Regs -------------\n");
	for (i = 0; i < times; i++) {
		INT32 k = 0;

		for (; k < DMA_REGS_MAX; k++) {
			STP_DBG_WARN_FUNC("times:%d,i:%d reg: %s, regs:%08x\n", times, i, dmaRegsStr[k],
					  wmt_plat_read_dmaregs(k));
			/* g_stp_dbg_dmaregs->dmaIssue[k][g_stp_dbg_dmaregs->count + i] = wmt_plat_read_dmaregs(k); */
		}
		osal_sleep_ms(sleep);
	}
	STP_DBG_WARN_FUNC("---------Polling DMA relative Regs End-------------\n");
	g_stp_dbg_dmaregs->count += times;

	osal_unlock_sleepable_lock(&g_stp_dbg_dmaregs->lock);
#else
	return 0;
#endif
}

INT32 stp_dbg_poll_cuppcr_ctrl(UINT32 en)
{

	STP_DBG_INFO_FUNC("%s polling cpupcr\n", en == 0 ? "start" : "stop");

	osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
	g_stp_dbg_cpupcr->stop_flag = en;
	osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
	return 0;
}

INT32 stp_dbg_set_version_info(UINT32 chipid, UINT8 *pRomVer, UINT8 *pPatchVer, UINT8 *pPatchBrh)
{
	if (g_stp_dbg_cpupcr) {
		osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
		g_stp_dbg_cpupcr->chipId = chipid;

		if (pRomVer)
			osal_memcpy(g_stp_dbg_cpupcr->romVer, pRomVer, 2);
		if (pPatchVer)
			osal_memcpy(g_stp_dbg_cpupcr->patchVer, pPatchVer, 8);
		if (pPatchBrh)
			osal_memcpy(g_stp_dbg_cpupcr->branchVer, pPatchBrh, 4);

		osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
	} else {
		STP_DBG_ERR_FUNC("NULL pointer\n");
		return -1;
	}
	STP_DBG_INFO_FUNC("chipid(0x%x),romver(%s),patchver(%s),branchver(%s)\n", g_stp_dbg_cpupcr->chipId,
		&g_stp_dbg_cpupcr->romVer[0], &g_stp_dbg_cpupcr->patchVer[0], &g_stp_dbg_cpupcr->branchVer[0]);
	return 0;
}
INT32 stp_dbg_set_wifiver(UINT32 wifiver)
{
	if (g_stp_dbg_cpupcr) {
		osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
		g_stp_dbg_cpupcr->wifiVer = wifiver;
		osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
	} else {
		STP_DBG_ERR_FUNC("NULL pointer\n");
		return -1;
	}
	STP_DBG_INFO_FUNC("wifiver(%x)\n", g_stp_dbg_cpupcr->wifiVer);
	return 0;
}

INT32 stp_dbg_set_host_assert_info(UINT32 drv_type, UINT32 reason, UINT32 en)
{
	osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);

	g_stp_dbg_cpupcr->host_assert_info.assert_from_host = en;
	g_stp_dbg_cpupcr->host_assert_info.drv_type = drv_type;
	g_stp_dbg_cpupcr->host_assert_info.reason = reason;

	osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);

	return 0;
}

UINT32 stp_dbg_get_host_trigger_assert(VOID)
{
	return g_stp_dbg_cpupcr->host_assert_info.assert_from_host;
}

INT32 stp_dbg_set_fw_info(UINT8 *issue_info, UINT32 len, ENUM_STP_FW_ISSUE_TYPE issue_type)
{
	ENUM_ASSERT_INFO_PARSER_TYPE type_index;
	PUINT8 tempbuf = NULL;
	UINT32 i = 0;
	INT32 iRet = 0;

	if (NULL == issue_info) {
		STP_DBG_ERR_FUNC("null issue infor\n");
		return -1;
	}
	STP_DBG_INFO_FUNC("issue type(%d)\n", issue_type);
	g_stp_dbg_cpupcr->issue_type = issue_type;
	osal_memset(&g_stp_dbg_cpupcr->assert_info[0], 0, STP_ASSERT_INFO_SIZE);

	/*print patch version when assert happened */
	STP_DBG_INFO_FUNC("=======================================\n");
	STP_DBG_INFO_FUNC("[consys patch]patch version:%s\n", g_stp_dbg_cpupcr->patchVer);
	STP_DBG_INFO_FUNC("[consys patch]ALPS branch:%s\n", g_stp_dbg_cpupcr->branchVer);
	STP_DBG_INFO_FUNC("=======================================\n");

	if ((STP_FW_ASSERT_ISSUE == issue_type) ||
	    (STP_HOST_TRIGGER_FW_ASSERT == issue_type) || (STP_HOST_TRIGGER_ASSERT_TIMEOUT == issue_type)) {
		if ((STP_FW_ASSERT_ISSUE == issue_type) || (STP_HOST_TRIGGER_FW_ASSERT == issue_type)) {
			tempbuf = osal_malloc(len + 1);
			if (!tempbuf)
				return -2;

			osal_memcpy(&tempbuf[0], issue_info, len);

			for (i = 0; i < len; i++) {
				if (tempbuf[i] == '\0')
					tempbuf[i] = '?';
			}

			tempbuf[len] = '\0';

			for (type_index = STP_DBG_ASSERT_INFO; type_index < STP_DBG_PARSER_TYPE_MAX; type_index++)
				iRet += _stp_dbg_parser_assert_str(&tempbuf[0], type_index);

			if (iRet)
				STP_DBG_ERR_FUNC("passert assert infor fail(%d)\n", iRet);

		}
		if ((STP_HOST_TRIGGER_FW_ASSERT == issue_type) || (STP_HOST_TRIGGER_ASSERT_TIMEOUT == issue_type)) {
			switch (g_stp_dbg_cpupcr->host_assert_info.drv_type) {
			case 0:
				STP_DBG_INFO_FUNC("BT trigger assert\n");
				osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
				if (31 != g_stp_dbg_cpupcr->host_assert_info.reason)
					/*BT firmware trigger assert */
				{
					g_stp_dbg_cpupcr->fwTaskId = 1;

				} else
					/*BT stack trigger assert */
				{
					g_stp_dbg_cpupcr->fwTaskId = 8;
				}

				g_stp_dbg_cpupcr->host_assert_info.assert_from_host = 0;
				/* g_stp_dbg_cpupcr->host_assert_info.drv_type = 0; */
				/* g_stp_dbg_cpupcr->host_assert_info.reason = 0; */

				osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);

				break;
			case 4:
				STP_DBG_INFO_FUNC("WMT trigger assert\n");
				osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
				if (STP_HOST_TRIGGER_ASSERT_TIMEOUT == issue_type)
					osal_memcpy(&g_stp_dbg_cpupcr->assert_info[0], issue_info, len);

				if ((38 == g_stp_dbg_cpupcr->host_assert_info.reason) ||
				    (39 == g_stp_dbg_cpupcr->host_assert_info.reason) ||
				    (40 == g_stp_dbg_cpupcr->host_assert_info.reason))
					g_stp_dbg_cpupcr->fwTaskId = 6;	/* HOST schedule reason trigger */
				else
					g_stp_dbg_cpupcr->fwTaskId = 0;	/* Must be firmware reason */
				g_stp_dbg_cpupcr->host_assert_info.assert_from_host = 0;
				osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
				break;
			default:
				break;
			}

		}
		osal_free(tempbuf);
	} else if (STP_FW_NOACK_ISSUE == issue_type) {
		osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
		osal_memcpy(&g_stp_dbg_cpupcr->assert_info[0], issue_info, len);
		g_stp_dbg_cpupcr->fwTaskId = 6;
		g_stp_dbg_cpupcr->fwRrq = 0;
		g_stp_dbg_cpupcr->fwIsr = 0;
		osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
	} else if (STP_DBG_PROC_TEST == issue_type) {
		osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
		osal_memcpy(&g_stp_dbg_cpupcr->assert_info[0], issue_info, len);
		g_stp_dbg_cpupcr->fwTaskId = 0;
		g_stp_dbg_cpupcr->fwRrq = 0;
		g_stp_dbg_cpupcr->fwIsr = 0;
		osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
	} else if (STP_FW_WARM_RST_ISSUE == issue_type) {
		osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
		osal_memcpy(&g_stp_dbg_cpupcr->assert_info[0], issue_info, len);
		g_stp_dbg_cpupcr->fwTaskId = 0;
		g_stp_dbg_cpupcr->fwRrq = 0;
		g_stp_dbg_cpupcr->fwIsr = 0;
		osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
	} else {
		STP_DBG_ERR_FUNC("invalid issue type(%d)\n", issue_type);
		return -3;
	}

	return iRet;
}

INT32 stp_dbg_cpupcr_infor_format(PPUINT8 buf, PUINT32 str_len)
{
	UINT32 len = 0;
	UINT32 i = 0;

	if (!g_stp_dbg_cpupcr) {
		STP_DBG_ERR_FUNC("NULL pointer\n");
		return -1;
	}

	/*format common information about issue */
	len = osal_sprintf(*buf, "<main>\n\t");
	len += osal_sprintf(*buf + len, "<chipid>\n\t\tMT%x\n\t</chipid>\n\t", g_stp_dbg_cpupcr->chipId);
	len += osal_sprintf(*buf + len, "<version>\n\t\t");
	len += osal_sprintf(*buf + len, "<rom>%s</rom>\n\t\t", g_stp_dbg_cpupcr->romVer);
	if (!(osal_memcmp(g_stp_dbg_cpupcr->branchVer, "ALPS", strlen("ALPS"))))
		len += osal_sprintf(*buf + len, "<branch>Internal Dev</branch>\n\t\t", g_stp_dbg_cpupcr->branchVer);
	else
		len += osal_sprintf(*buf + len, "<branch>W%sMP</branch>\n\t\t", g_stp_dbg_cpupcr->branchVer);

	len += osal_sprintf(*buf + len, "<patch>%s</patch>\n\t\t", g_stp_dbg_cpupcr->patchVer);

	if (0 == g_stp_dbg_cpupcr->wifiVer)
		len += osal_sprintf(*buf + len, "<wifi>NULL</wifi>\n\t");
	else
		len += osal_sprintf(*buf + len, "<wifi>0x%X.%X</wifi>\n\t",
		(UINT8)((g_stp_dbg_cpupcr->wifiVer & 0xFF00)>>8), (UINT8)(g_stp_dbg_cpupcr->wifiVer & 0xFF));

	len += osal_sprintf(*buf + len, "</version>\n\t");

	/*format issue information: no ack, assert */
	len += osal_sprintf(*buf + len, "<issue>\n\t\t<classification>\n\t\t\t");
	if ((STP_FW_NOACK_ISSUE == g_stp_dbg_cpupcr->issue_type) ||
	    (STP_DBG_PROC_TEST == g_stp_dbg_cpupcr->issue_type) ||
	    (STP_FW_WARM_RST_ISSUE == g_stp_dbg_cpupcr->issue_type)) {
		len +=
		    osal_sprintf(*buf + len, "%s\n\t\t</classification>\n\t\t<rc>\n\t\t\t",
				 g_stp_dbg_cpupcr->assert_info);
		len += osal_sprintf(*buf + len, "NULL\n\t\t</rc>\n\t</issue>\n\t");
		len += osal_sprintf(*buf + len, "<hint>\n\t\t<time_align>NULL</time_align>\n\t\t");
		len += osal_sprintf(*buf + len, "<host>NULL</host>\n\t\t");
		len +=
		    osal_sprintf(*buf + len, "<client>\n\t\t\t<task>%s</task>\n\t\t\t",
				 _stp_dbg_id_to_task(g_stp_dbg_cpupcr->fwTaskId));
		len += osal_sprintf(*buf + len, "<irqx>IRQ_0x%x</irqx>\n\t\t\t", g_stp_dbg_cpupcr->fwRrq);
		len += osal_sprintf(*buf + len, "<isr>0x%x</isr>\n\t\t\t", g_stp_dbg_cpupcr->fwIsr);
		len += osal_sprintf(*buf + len, "<drv_type>NULL</drv_type>\n\t\t\t");
		len += osal_sprintf(*buf + len, "<reason>NULL</reason>\n\t\t\t");
	} else if ((STP_FW_ASSERT_ISSUE == g_stp_dbg_cpupcr->issue_type) ||
		   (STP_HOST_TRIGGER_FW_ASSERT == g_stp_dbg_cpupcr->issue_type) ||
		   (STP_HOST_TRIGGER_ASSERT_TIMEOUT == g_stp_dbg_cpupcr->issue_type)) {
		len +=
		    osal_sprintf(*buf + len, "%s\n\t\t</classification>\n\t\t<rc>\n\t\t\t",
				 g_stp_dbg_cpupcr->assert_info);
		len += osal_sprintf(*buf + len, "%s\n\t\t</rc>\n\t</issue>\n\t", g_stp_dbg_cpupcr->assert_type);
		len += osal_sprintf(*buf + len, "<hint>\n\t\t<time_align>NULL</time_align>\n\t\t");
		len += osal_sprintf(*buf + len, "<host>NULL</host>\n\t\t");
		len +=
		    osal_sprintf(*buf + len, "<client>\n\t\t\t<task>%s</task>\n\t\t\t",
				 _stp_dbg_id_to_task(g_stp_dbg_cpupcr->fwTaskId));
		if (32 == g_stp_dbg_cpupcr->host_assert_info.reason || 33 == g_stp_dbg_cpupcr->host_assert_info.reason
		    || 34 == g_stp_dbg_cpupcr->host_assert_info.reason
		    || 35 == g_stp_dbg_cpupcr->host_assert_info.reason
		    || 36 == g_stp_dbg_cpupcr->host_assert_info.reason
		    || 37 == g_stp_dbg_cpupcr->host_assert_info.reason
		    || 38 == g_stp_dbg_cpupcr->host_assert_info.reason
		    || 39 == g_stp_dbg_cpupcr->host_assert_info.reason
		    || 40 == g_stp_dbg_cpupcr->host_assert_info.reason) {
			/*handling wmt turn on/off bt cmd has ack but no evt issue */
			/*one of both the irqx and irs is nULL, then use task to find MOF */
			len += osal_sprintf(*buf + len, "<irqx>NULL</irqx>\n\t\t\t");
		} else {
			len += osal_sprintf(*buf + len, "<irqx>IRQ_0x%x</irqx>\n\t\t\t", g_stp_dbg_cpupcr->fwRrq);
		}
		len += osal_sprintf(*buf + len, "<isr>0x%x</isr>\n\t\t\t", g_stp_dbg_cpupcr->fwIsr);

		if (STP_FW_ASSERT_ISSUE == g_stp_dbg_cpupcr->issue_type) {
			len += osal_sprintf(*buf + len, "<drv_type>NULL</drv_type>\n\t\t\t");
			len += osal_sprintf(*buf + len, "<reason>NULL</reason>\n\t\t\t");
		}

		if ((STP_HOST_TRIGGER_FW_ASSERT == g_stp_dbg_cpupcr->issue_type) ||
		    (STP_HOST_TRIGGER_ASSERT_TIMEOUT == g_stp_dbg_cpupcr->issue_type)) {
			len +=
			    osal_sprintf(*buf + len, "<drv_type>%d</drv_type>\n\t\t\t",
					 g_stp_dbg_cpupcr->host_assert_info.drv_type);
			len +=
			    osal_sprintf(*buf + len, "<reason>%d</reason>\n\t\t\t",
					 g_stp_dbg_cpupcr->host_assert_info.reason);
		}
	} else {
		len += osal_sprintf(*buf + len, "NULL\n\t\t</classification>\n\t\t<rc>\n\t\t\t");
		len += osal_sprintf(*buf + len, "NULL\n\t\t</rc>\n\t</issue>\n\t");
		len += osal_sprintf(*buf + len, "<hint>\n\t\t<time_align>NULL</time_align>\n\t\t");
		len += osal_sprintf(*buf + len, "<host>NULL</host>\n\t\t");
		len += osal_sprintf(*buf + len, "<client>\n\t\t\t<task>NULL</task>\n\t\t\t");
		len += osal_sprintf(*buf + len, "<irqx>NULL</irqx>\n\t\t\t");
		len += osal_sprintf(*buf + len, "<isr>NULL</isr>\n\t\t\t");
		len += osal_sprintf(*buf + len, "<drv_type>NULL</drv_type>\n\t\t\t");
		len += osal_sprintf(*buf + len, "<reason>NULL</reason>\n\t\t\t");
	}

	len += osal_sprintf(*buf + len, "<pctrace>");
	STP_DBG_INFO_FUNC("stp-dbg:sub len1 for debug(%d)\n", len);

	if (!g_stp_dbg_cpupcr->count)
		len += osal_sprintf(*buf + len, "NULL");
	else {
		for (i = 0; i < g_stp_dbg_cpupcr->count; i++)
			len += osal_sprintf(*buf + len, "%08x,", g_stp_dbg_cpupcr->buffer[i]);
	}
	STP_DBG_INFO_FUNC("stp-dbg:sub len2 for debug(%d)\n", len);
	len += osal_sprintf(*buf + len, "</pctrace>\n\t\t\t");
	len += osal_sprintf(*buf + len, "<extension>NULL</extension>\n\t\t</client>\n\t</hint>\n</main>\n");
	STP_DBG_INFO_FUNC("buffer len[%d]\n", len);
	/* STP_DBG_INFO_FUNC("Format infor:\n%s\n",*buf); */
	*str_len = len;

	osal_lock_sleepable_lock(&g_stp_dbg_cpupcr->lock);
	osal_memset(&g_stp_dbg_cpupcr->buffer[0], 0, STP_DBG_CPUPCR_NUM);
	g_stp_dbg_cpupcr->count = 0;
	g_stp_dbg_cpupcr->host_assert_info.reason = 0;
	g_stp_dbg_cpupcr->host_assert_info.drv_type = 0;
	osal_unlock_sleepable_lock(&g_stp_dbg_cpupcr->lock);

	return 0;

}

MTKSTP_DBG_T *stp_dbg_init(void *btm_half)
{

	MTKSTP_DBG_T *stp_dbg = NULL;

	STP_DBG_INFO_FUNC("stp-dbg init\n");

	stp_dbg = kzalloc(sizeof(MTKSTP_DBG_T), GFP_KERNEL);
	if (stp_dbg == NULL)
		goto ERR_EXIT1;
	if (IS_ERR(stp_dbg)) {
		STP_DBG_ERR_FUNC("-ENOMEM\n");
		goto ERR_EXIT1;
	}

	stp_dbg->logsys = vmalloc(sizeof(MTKSTP_LOG_SYS_T));
	if (stp_dbg->logsys == NULL)
		goto ERR_EXIT2;
	if (IS_ERR(stp_dbg->logsys)) {
		STP_DBG_ERR_FUNC("-ENOMEM stp_gdb->logsys\n");
		goto ERR_EXIT2;
	}
	memset(stp_dbg->logsys, 0, sizeof(MTKSTP_LOG_SYS_T));
	spin_lock_init(&(stp_dbg->logsys->lock));
	stp_dbg->pkt_trace_no = 0;
	stp_dbg->is_enable = 0;
	g_stp_dbg = stp_dbg;

	if (btm_half != NULL)
		stp_dbg->btm = btm_half;
	else
		stp_dbg->btm = NULL;


	/* bind to netlink */
	stp_dbg_nl_init();
	g_core_dump = wcn_core_dump_init(STP_CORE_DUMP_INIT_SIZE, STP_CORE_DUMP_TIMEOUT);
	g_stp_dbg_cpupcr = stp_dbg_cpupcr_init();
	g_stp_dbg_dmaregs = stp_dbg_dmaregs_init();

	return stp_dbg;

ERR_EXIT2:
	kfree(stp_dbg);
	return NULL;

ERR_EXIT1:
	kfree(stp_dbg);
	return NULL;
}

int stp_dbg_deinit(MTKSTP_DBG_T *stp_dbg)
{

	STP_DBG_INFO_FUNC("stp-dbg deinit\n");

	wcn_core_dump_deinit(g_core_dump);

	stp_dbg_cpupcr_deinit(g_stp_dbg_cpupcr);
	stp_dbg_dmaregs_deinit(g_stp_dbg_dmaregs);
	/* unbind with netlink */
	stp_dbg_nl_deinit();

	if (stp_dbg->logsys)
		vfree(stp_dbg->logsys);

	kfree(stp_dbg);

	return 0;
}
