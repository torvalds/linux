/* mt6628_fm_link.c
 *
 * (C) Copyright 2009
 * MediaTek <www.MediaTek.com>
 * Hongcheng <hongcheng.xia@MediaTek.com>
 *
 * MT6628 FM Radio Driver -- setup data link
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 #if 0
#include <linux/slab.h>
#include <linux/interrupt.h>

#include "stp_exp.h"
#include "wmt_exp.h"

#include "fm_typedef.h"
#include "fm_dbg.h"
#include "fm_err.h"
#include "fm_stdlib.h"

#include "mt6628_fm.h"
#include "mt6628_fm_link.h"

static struct fm_link_event *link_event;

static struct fm_trace_fifo_t *cmd_fifo;

static struct fm_trace_fifo_t *evt_fifo;
    
static fm_s32 (*reset)(fm_s32 sta) = NULL;

static void mt6628_fm_wholechip_rst_cb(ENUM_WMTDRV_TYPE_T src,
                                ENUM_WMTDRV_TYPE_T dst,
                                ENUM_WMTMSG_TYPE_T type,
                                void *buf,
                                unsigned int sz)
{
    //To handle reset procedure please
    ENUM_WMTRSTMSG_TYPE_T rst_msg;
    
    if (sz <= sizeof(ENUM_WMTRSTMSG_TYPE_T)) {
        memcpy((char *)&rst_msg, (char *)buf, sz);
        WCN_DBG(FM_WAR | LINK, "[src=%d], [dst=%d], [type=%d], [buf=0x%x], [sz=%d], [max=%d]\n", src, dst, type, rst_msg, sz, WMTRSTMSG_RESET_MAX);

        if ((src == WMTDRV_TYPE_WMT) && (dst == WMTDRV_TYPE_FM) && (type == WMTMSG_TYPE_RESET)) {
            if (rst_msg == WMTRSTMSG_RESET_START) {
                WCN_DBG(FM_WAR | LINK, "FM restart start!\n");
                if (reset) {
                    reset(1);
                }
                
            } else if (rst_msg == WMTRSTMSG_RESET_END) {
                WCN_DBG(FM_WAR | LINK, "FM restart end!\n");
                if (reset) {
                    reset(0);
                }
            }
        }
    } else {
        /*message format invalid*/
        WCN_DBG(FM_WAR | LINK, "message format invalid!\n");
    }
}

    
fm_s32 fm_link_setup(void* data)
{
    fm_s32 ret = 0;
    
    if (!(link_event = fm_zalloc(sizeof(struct fm_link_event)))) {
        WCN_DBG(FM_ALT | LINK, "fm_zalloc(fm_link_event) -ENOMEM\n");
        return -1;
    }

    link_event->ln_event = fm_flag_event_create("ln_evt");

    if (!link_event->ln_event) {
        WCN_DBG(FM_ALT | LINK, "create mt6628_ln_event failed\n");
        fm_free(link_event);
        return -1;
    }

    fm_flag_event_get(link_event->ln_event);


    WCN_DBG(FM_NTC | LINK, "fm link setup\n");

    cmd_fifo = fm_trace_fifo_create("cmd_fifo");
    if (!cmd_fifo) {
        WCN_DBG(FM_ALT | LINK, "create cmd_fifo failed\n");
        ret = -1;
        goto failed;
    }
    
    evt_fifo = fm_trace_fifo_create("evt_fifo");
    if (!evt_fifo) {
        WCN_DBG(FM_ALT | LINK, "create evt_fifo failed\n");
        ret = -1;
        goto failed;
    }
    
    reset = data; // get whole chip reset cb
    mtk_wcn_wmt_msgcb_reg(WMTDRV_TYPE_FM, mt6628_fm_wholechip_rst_cb);
    return 0;
    
failed:
    fm_trace_fifo_release(evt_fifo);
    fm_trace_fifo_release(cmd_fifo);
    fm_flag_event_put(link_event->ln_event);
    if (link_event) {
        fm_free(link_event);
    }
    
    return ret;
}

fm_s32 fm_link_release(void)
{

    fm_trace_fifo_release(evt_fifo);
    fm_trace_fifo_release(cmd_fifo);
    fm_flag_event_put(link_event->ln_event);
    if (link_event) {
        fm_free(link_event);
    }

    WCN_DBG(FM_NTC | LINK, "fm link release\n");
    return 0;
}

/*
 * fm_ctrl_rx
 * the low level func to read a rigister
 * @addr - rigister address
 * @val - the pointer of target buf
 * If success, return 0; else error code
 */
fm_s32 fm_ctrl_rx(fm_u8 addr, fm_u16 *val)
{
    return 0;
}

/*
 * fm_ctrl_tx
 * the low level func to write a rigister
 * @addr - rigister address
 * @val - value will be writed in the rigister
 * If success, return 0; else error code
 */
fm_s32 fm_ctrl_tx(fm_u8 addr, fm_u16 val)
{
    return 0;
}

/*
 * fm_cmd_tx() - send cmd to FM firmware and wait event
 * @buf - send buffer
 * @len - the length of cmd
 * @mask - the event flag mask
 * @	cnt - the retry conter
 * @timeout - timeout per cmd
 * Return 0, if success; error code, if failed
 */
fm_s32 fm_cmd_tx(fm_u8* buf, fm_u16 len, fm_s32 mask, fm_s32 cnt, fm_s32 timeout, fm_s32(*callback)(struct fm_res_ctx* result))
{
    fm_s32 ret_time = 0;
    struct task_struct *task = current;
    struct fm_trace_t trace;

    if ((NULL == buf) || (len < 0) || (0 == mask)
            || (cnt > SW_RETRY_CNT_MAX) || (timeout > SW_WAIT_TIMEOUT_MAX)) {
        WCN_DBG(FM_ERR | LINK, "cmd tx, invalid para\n");
        return -FM_EPARA;
    }

    FM_EVENT_CLR(link_event->ln_event, mask);
    
#ifdef FM_TRACE_ENABLE
    trace.type = buf[0];
    trace.opcode = buf[1];
    trace.len = len - 4;
    trace.tid = (fm_s32)task->pid;
    fm_memset(trace.pkt, 0, FM_TRACE_PKT_SIZE);
    fm_memcpy(trace.pkt, &buf[4], (trace.len > FM_TRACE_PKT_SIZE) ? FM_TRACE_PKT_SIZE : trace.len);
#endif

sw_retry:

#ifdef FM_TRACE_ENABLE
    if (fm_true == FM_TRACE_FULL(cmd_fifo)) {
        FM_TRACE_OUT(cmd_fifo, NULL);
    }
    FM_TRACE_IN(cmd_fifo, &trace);
#endif

    //send cmd to FM firmware
    if (mtk_wcn_stp_send_data(buf, len, FM_TASK_INDX) == 0) {
        WCN_DBG(FM_EMG | LINK, "send data over stp failed\n");
        return -FM_ELINK;
    }

    //wait the response form FM firmware
    ret_time = FM_EVENT_WAIT_TIMEOUT(link_event->ln_event, mask, timeout);

    if (!ret_time) {
        if (0 < cnt--) {
            WCN_DBG(FM_WAR | LINK, "wait even timeout, [retry_cnt=%d], pid=%d\n", cnt, task->pid);
            fm_print_cmd_fifo();
            fm_print_evt_fifo();
            return -FM_EFW;
            goto sw_retry; //retry if timeout and retry cnt > 0
        } else {
            WCN_DBG(FM_ALT | LINK, "fatal error, SW retry failed, reset HW\n");
            return -FM_EFW;
        }
    }

    FM_EVENT_CLR(link_event->ln_event, mask);

    if (callback) {
        callback(&link_event->result);
    }

    return 0;
}

fm_s32 fm_event_parser(fm_s32(*rds_parser)(struct rds_rx_t*, fm_s32))
{
    fm_s32 len;
    fm_s32 i = 0;
    fm_u8 opcode = 0;
    fm_u16 length = 0;
    fm_u8 ch;
    fm_u8 rx_buf[RX_BUF_SIZE + 10] = {0}; //the 10 bytes are protect gaps
    static volatile fm_task_parser_state state = FM_TASK_RX_PARSER_PKT_TYPE;
    struct fm_trace_t trace;
    struct task_struct *task = current;

    len = mtk_wcn_stp_receive_data(rx_buf, RX_BUF_SIZE, FM_TASK_INDX);
    WCN_DBG(FM_DBG | LINK, "[len=%d],[CMD=0x%02x 0x%02x 0x%02x 0x%02x]\n", len, rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);

    while (i < len) {
        ch = rx_buf[i];

        switch (state) {
        case FM_TASK_RX_PARSER_PKT_TYPE:

            if (ch == FM_TASK_EVENT_PKT_TYPE) {
                if ((i + 5) < RX_BUF_SIZE) {
                    WCN_DBG(FM_DBG | LINK, "0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x \n", rx_buf[i], rx_buf[i+1], rx_buf[i+2], rx_buf[i+3], rx_buf[i+4], rx_buf[i+5]);
                } else {
                    WCN_DBG(FM_DBG | LINK, "0x%02x 0x%02x\n", rx_buf[i], rx_buf[i+1]);
                }

                state = FM_TASK_RX_PARSER_OPCODE;
            } else {
                WCN_DBG(FM_ALT | LINK, "event pkt type error (rx_buf[%d] = 0x%02x)\n", i, ch);
            }

            i++;
            break;

        case FM_TASK_RX_PARSER_OPCODE:
            i++;
            opcode = ch;
            state = FM_TASK_RX_PARSER_PKT_LEN_1;
            break;

        case FM_TASK_RX_PARSER_PKT_LEN_1:
            i++;
            length = ch;
            state = FM_TASK_RX_PARSER_PKT_LEN_2;
            break;

        case FM_TASK_RX_PARSER_PKT_LEN_2:
            i++;
            length |= (fm_u16)(ch << 0x8);

#ifdef FM_TRACE_ENABLE
            trace.type = FM_TASK_EVENT_PKT_TYPE;
            trace.opcode = opcode;
            trace.len = length;
            trace.tid = (fm_s32)task->pid;
            fm_memset(trace.pkt, 0, FM_TRACE_PKT_SIZE);
            fm_memcpy(trace.pkt, &rx_buf[i], (length > FM_TRACE_PKT_SIZE) ? FM_TRACE_PKT_SIZE : length);

            if (fm_true == FM_TRACE_FULL(cmd_fifo)) {
                FM_TRACE_OUT(cmd_fifo, NULL);
            }
            FM_TRACE_IN(cmd_fifo, &trace);
#endif
            if (length > 0) {
                state = FM_TASK_RX_PARSER_PKT_PAYLOAD;
            } else {
                state = FM_TASK_RX_PARSER_PKT_TYPE;
                FM_EVENT_SEND(link_event->ln_event, (1 << opcode));
            }

            break;

        case FM_TASK_RX_PARSER_PKT_PAYLOAD:

            switch (opcode) {
            case FM_TUNE_OPCODE:

                if ((length == 1) && (rx_buf[i] == 1)) {
                    FM_EVENT_SEND(link_event->ln_event, FLAG_TUNE_DONE);
                }

                break;

            case FM_SOFT_MUTE_TUNE_OPCODE:

                if (length >= 2) {
                    fm_memcpy(link_event->result.cqi, &rx_buf[i], (length > FM_CQI_BUF_SIZE) ? FM_CQI_BUF_SIZE : length);
                    FM_EVENT_SEND(link_event->ln_event, FLAG_SM_TUNE);
                }
                break;
                
            case FM_SEEK_OPCODE:

                if ((i + 1) < RX_BUF_SIZE) {
                    link_event->result.seek_result = rx_buf[i] + (rx_buf[i+1] << 8); // 8760 means 87.60Mhz
                }

                FM_EVENT_SEND(link_event->ln_event, FLAG_SEEK_DONE);
                break;

            case FM_SCAN_OPCODE:

                //check if the result data is long enough
                if ((RX_BUF_SIZE - i) < (sizeof(fm_u16) * FM_SCANTBL_SIZE)) {
                    WCN_DBG(FM_ALT | LINK, "FM_SCAN_OPCODE err, [tblsize=%d],[bufsize=%d]\n", (sizeof(fm_u16) * FM_SCANTBL_SIZE), (RX_BUF_SIZE - i));
                    FM_EVENT_SEND(link_event->ln_event, FLAG_SCAN_DONE);
                    return 0;
                } else if ((length >= FM_CQI_BUF_SIZE) && ((RX_BUF_SIZE - i) >= FM_CQI_BUF_SIZE)) {
                    fm_memcpy(link_event->result.cqi, &rx_buf[i], FM_CQI_BUF_SIZE);
                    FM_EVENT_SEND(link_event->ln_event, FLAG_CQI_DONE);
                } else {
                    fm_memcpy(link_event->result.scan_result, &rx_buf[i], sizeof(fm_u16) * FM_SCANTBL_SIZE);
                    FM_EVENT_SEND(link_event->ln_event, FLAG_SCAN_DONE);
                }

                break;

            case FSPI_READ_OPCODE:

                if ((i + 1) < RX_BUF_SIZE) {
                    link_event->result.fspi_rd = (rx_buf[i] + (rx_buf[i+1] << 8));
                }

                FM_EVENT_SEND(link_event->ln_event, (1 << opcode));
                break;

            case RDS_RX_DATA_OPCODE:

                //check if the rds data is long enough
                if ((RX_BUF_SIZE - i) < length) {
                    WCN_DBG(FM_ALT | LINK, "RDS RX err, [rxlen=%d],[bufsize=%d]\n", (fm_s32)length, (RX_BUF_SIZE - i));
                    FM_EVENT_SEND(link_event->ln_event, (1 << opcode));
                    break;
                }

                //copy rds data to rds buf
                fm_memcpy(&link_event->result.rds_rx_result, &rx_buf[i], length);

                /*Handle the RDS data that we get*/
                if (rds_parser) {
                    rds_parser(&link_event->result.rds_rx_result, length);
                } else {
                    WCN_DBG(FM_WAR | LINK, "no method to parse RDS data\n");
                }

                FM_EVENT_SEND(link_event->ln_event, (1 << opcode));
                break;

            default:
                FM_EVENT_SEND(link_event->ln_event, (1 << opcode));
                break;
            }

            state = FM_TASK_RX_PARSER_PKT_TYPE;
            i += length;
            break;

        default:
            break;
        }
    }

    return 0;
}

fm_bool fm_wait_stc_done(fm_u32 sec)
{
    return fm_true;
}

fm_s32 fm_force_active_event(fm_u32 mask)
{
    fm_u32 flag;

    flag = FM_EVENT_GET(link_event->ln_event);
    WCN_DBG(FM_WAR | LINK, "before force active event, [flag=0x%08x]\n", flag);
    flag = FM_EVENT_SEND(link_event->ln_event, mask);
    WCN_DBG(FM_WAR | LINK, "after force active event, [flag=0x%08x]\n", flag);

    return 0;
}


extern fm_s32 fm_print_cmd_fifo(void)
{
#ifdef FM_TRACE_ENABLE
    struct fm_trace_t trace;
    fm_s32 i = 0;
    
    while (fm_false == FM_TRACE_EMPTY(cmd_fifo)) {
        fm_memset(trace.pkt, 0, FM_TRACE_PKT_SIZE);
        FM_TRACE_OUT(cmd_fifo, &trace);
        WCN_DBG(FM_ALT | LINK, "trace, type %d, op %d, len %d, tid %d, time %d\n", trace.type, trace.opcode, trace.len, trace.tid, jiffies_to_msecs(abs(trace.time)));
        i = 0;
        while ((trace.len > 0) && (i < trace.len) && (i < (FM_TRACE_PKT_SIZE-8))) {
            WCN_DBG(FM_ALT | LINK, "trace, %02x %02x %02x %02x %02x %02x %02x %02x\n", \
                trace.pkt[i], trace.pkt[i+1], trace.pkt[i+2], trace.pkt[i+3], trace.pkt[i+4], trace.pkt[i+5], trace.pkt[i+6], trace.pkt[i+7]);
            i += 8;
        }
        WCN_DBG(FM_ALT | LINK, "trace\n");
    }
#endif

    return 0;
}

extern fm_s32 fm_print_evt_fifo(void)
{
#ifdef FM_TRACE_ENABLE
    struct fm_trace_t trace;
    fm_s32 i = 0;
    
    while (fm_false == FM_TRACE_EMPTY(evt_fifo)) {
        fm_memset(trace.pkt, 0, FM_TRACE_PKT_SIZE);
        FM_TRACE_OUT(evt_fifo, &trace);
        WCN_DBG(FM_ALT | LINK, "%s: op %d, len %d, %d\n", evt_fifo->name, trace.opcode, trace.len, jiffies_to_msecs(abs(trace.time)));
        i = 0;
        while ((trace.len > 0) && (i < trace.len) && (i < (FM_TRACE_PKT_SIZE-8))) {
            WCN_DBG(FM_ALT | LINK, "%s: %02x %02x %02x %02x %02x %02x %02x %02x\n", \
                evt_fifo->name, trace.pkt[i], trace.pkt[i+1], trace.pkt[i+2], trace.pkt[i+3], trace.pkt[i+4], trace.pkt[i+5], trace.pkt[i+6], trace.pkt[i+7]);
            i += 8;
        }
        WCN_DBG(FM_ALT | LINK, "%s\n", evt_fifo->name);
    }
#endif

    return 0;
}
#endif
