/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _PSP_TEE_GFX_IF_H_
#define _PSP_TEE_GFX_IF_H_

#define PSP_GFX_CMD_BUF_VERSION     0x00000001

#define GFX_CMD_STATUS_MASK         0x0000FFFF
#define GFX_CMD_ID_MASK             0x000F0000
#define GFX_CMD_RESERVED_MASK       0x7FF00000
#define GFX_CMD_RESPONSE_MASK       0x80000000

/* TEE Gfx Command IDs for the register interface.
*  Command ID must be between 0x00010000 and 0x000F0000.
*/
enum psp_gfx_crtl_cmd_id
{
    GFX_CTRL_CMD_ID_INIT_RBI_RING   = 0x00010000,   /* initialize RBI ring */
    GFX_CTRL_CMD_ID_INIT_GPCOM_RING = 0x00020000,   /* initialize GPCOM ring */
    GFX_CTRL_CMD_ID_DESTROY_RINGS   = 0x00030000,   /* destroy rings */
    GFX_CTRL_CMD_ID_CAN_INIT_RINGS  = 0x00040000,   /* is it allowed to initialized the rings */
    GFX_CTRL_CMD_ID_ENABLE_INT      = 0x00050000,   /* enable PSP-to-Gfx interrupt */
    GFX_CTRL_CMD_ID_DISABLE_INT     = 0x00060000,   /* disable PSP-to-Gfx interrupt */
    GFX_CTRL_CMD_ID_MODE1_RST       = 0x00070000,   /* trigger the Mode 1 reset */

    GFX_CTRL_CMD_ID_MAX             = 0x000F0000,   /* max command ID */
};


/*-----------------------------------------------------------------------------
    NOTE:   All physical addresses used in this interface are actually
            GPU Virtual Addresses.
*/


/* Control registers of the TEE Gfx interface. These are located in
*  SRBM-to-PSP mailbox registers (total 8 registers).
*/
struct psp_gfx_ctrl
{
    volatile uint32_t   cmd_resp;         /* +0   Command/Response register for Gfx commands */
    volatile uint32_t   rbi_wptr;         /* +4   Write pointer (index) of RBI ring */
    volatile uint32_t   rbi_rptr;         /* +8   Read pointer (index) of RBI ring */
    volatile uint32_t   gpcom_wptr;       /* +12  Write pointer (index) of GPCOM ring */
    volatile uint32_t   gpcom_rptr;       /* +16  Read pointer (index) of GPCOM ring */
    volatile uint32_t   ring_addr_lo;     /* +20  bits [31:0] of GPU Virtual of ring buffer (VMID=0)*/
    volatile uint32_t   ring_addr_hi;     /* +24  bits [63:32] of GPU Virtual of ring buffer (VMID=0) */
    volatile uint32_t   ring_buf_size;    /* +28  Ring buffer size (in bytes) */

};


/* Response flag is set in the command when command is completed by PSP.
*  Used in the GFX_CTRL.CmdResp.
*  When PSP GFX I/F is initialized, the flag is set.
*/
#define GFX_FLAG_RESPONSE               0x80000000


/* TEE Gfx Command IDs for the ring buffer interface. */
enum psp_gfx_cmd_id
{
    GFX_CMD_ID_LOAD_TA      = 0x00000001,   /* load TA */
    GFX_CMD_ID_UNLOAD_TA    = 0x00000002,   /* unload TA */
    GFX_CMD_ID_INVOKE_CMD   = 0x00000003,   /* send command to TA */
    GFX_CMD_ID_LOAD_ASD     = 0x00000004,   /* load ASD Driver */
    GFX_CMD_ID_SETUP_TMR    = 0x00000005,   /* setup TMR region */
    GFX_CMD_ID_LOAD_IP_FW   = 0x00000006,   /* load HW IP FW */
    GFX_CMD_ID_DESTROY_TMR  = 0x00000007,   /* destroy TMR region */
    GFX_CMD_ID_SAVE_RESTORE = 0x00000008,   /* save/restore HW IP FW */

};


/* Command to load Trusted Application binary into PSP OS. */
struct psp_gfx_cmd_load_ta
{
    uint32_t        app_phy_addr_lo;        /* bits [31:0] of the GPU Virtual address of the TA binary (must be 4 KB aligned) */
    uint32_t        app_phy_addr_hi;        /* bits [63:32] of the GPU Virtual address of the TA binary */
    uint32_t        app_len;                /* length of the TA binary in bytes */
    uint32_t        cmd_buf_phy_addr_lo;    /* bits [31:0] of the GPU Virtual address of CMD buffer (must be 4 KB aligned) */
    uint32_t        cmd_buf_phy_addr_hi;    /* bits [63:32] of the GPU Virtual address of CMD buffer */
    uint32_t        cmd_buf_len;            /* length of the CMD buffer in bytes; must be multiple of 4 KB */

    /* Note: CmdBufLen can be set to 0. In this case no persistent CMD buffer is provided
    *       for the TA. Each InvokeCommand can have dinamically mapped CMD buffer instead
    *       of using global persistent buffer.
    */
};


/* Command to Unload Trusted Application binary from PSP OS. */
struct psp_gfx_cmd_unload_ta
{
    uint32_t        session_id;          /* Session ID of the loaded TA to be unloaded */

};


/* Shared buffers for InvokeCommand.
*/
struct psp_gfx_buf_desc
{
    uint32_t        buf_phy_addr_lo;       /* bits [31:0] of GPU Virtual address of the buffer (must be 4 KB aligned) */
    uint32_t        buf_phy_addr_hi;       /* bits [63:32] of GPU Virtual address of the buffer */
    uint32_t        buf_size;              /* buffer size in bytes (must be multiple of 4 KB and no bigger than 64 MB) */

};

/* Max number of descriptors for one shared buffer (in how many different
*  physical locations one shared buffer can be stored). If buffer is too much
*  fragmented, error will be returned.
*/
#define GFX_BUF_MAX_DESC        64

struct psp_gfx_buf_list
{
    uint32_t                num_desc;                    /* number of buffer descriptors in the list */
    uint32_t                total_size;                  /* total size of all buffers in the list in bytes (must be multiple of 4 KB) */
    struct psp_gfx_buf_desc buf_desc[GFX_BUF_MAX_DESC];  /* list of buffer descriptors */

    /* total 776 bytes */
};

/* Command to execute InvokeCommand entry point of the TA. */
struct psp_gfx_cmd_invoke_cmd
{
    uint32_t                session_id;           /* Session ID of the TA to be executed */
    uint32_t                ta_cmd_id;            /* Command ID to be sent to TA */
    struct psp_gfx_buf_list buf;                  /* one indirect buffer (scatter/gather list) */

};


/* Command to setup TMR region. */
struct psp_gfx_cmd_setup_tmr
{
    uint32_t        buf_phy_addr_lo;       /* bits [31:0] of GPU Virtual address of TMR buffer (must be 4 KB aligned) */
    uint32_t        buf_phy_addr_hi;       /* bits [63:32] of GPU Virtual address of TMR buffer */
    uint32_t        buf_size;              /* buffer size in bytes (must be multiple of 4 KB) */

};


/* FW types for GFX_CMD_ID_LOAD_IP_FW command. Limit 31. */
enum psp_gfx_fw_type
{
    GFX_FW_TYPE_NONE        = 0,
    GFX_FW_TYPE_CP_ME       = 1,
    GFX_FW_TYPE_CP_PFP      = 2,
    GFX_FW_TYPE_CP_CE       = 3,
    GFX_FW_TYPE_CP_MEC      = 4,
    GFX_FW_TYPE_CP_MEC_ME1  = 5,
    GFX_FW_TYPE_CP_MEC_ME2  = 6,
    GFX_FW_TYPE_RLC_V       = 7,
    GFX_FW_TYPE_RLC_G       = 8,
    GFX_FW_TYPE_SDMA0       = 9,
    GFX_FW_TYPE_SDMA1       = 10,
    GFX_FW_TYPE_DMCU_ERAM   = 11,
    GFX_FW_TYPE_DMCU_ISR    = 12,
    GFX_FW_TYPE_VCN         = 13,
    GFX_FW_TYPE_UVD         = 14,
    GFX_FW_TYPE_VCE         = 15,
    GFX_FW_TYPE_ISP         = 16,
    GFX_FW_TYPE_ACP         = 17,
    GFX_FW_TYPE_SMU         = 18,
    GFX_FW_TYPE_MMSCH       = 19,
    GFX_FW_TYPE_RLC_RESTORE_LIST_GPM_MEM        = 20,
    GFX_FW_TYPE_RLC_RESTORE_LIST_SRM_MEM        = 21,
    GFX_FW_TYPE_RLC_RESTORE_LIST_CNTL           = 22,
    GFX_FW_TYPE_MAX         = 23
};

/* Command to load HW IP FW. */
struct psp_gfx_cmd_load_ip_fw
{
    uint32_t                fw_phy_addr_lo;    /* bits [31:0] of GPU Virtual address of FW location (must be 4 KB aligned) */
    uint32_t                fw_phy_addr_hi;    /* bits [63:32] of GPU Virtual address of FW location */
    uint32_t                fw_size;           /* FW buffer size in bytes */
    enum psp_gfx_fw_type    fw_type;           /* FW type */

};

/* Command to save/restore HW IP FW. */
struct psp_gfx_cmd_save_restore_ip_fw
{
    uint32_t                save_fw;              /* if set, command is used for saving fw otherwise for resetoring*/
    uint32_t                save_restore_addr_lo; /* bits [31:0] of FB address of GART memory used as save/restore buffer (must be 4 KB aligned) */
    uint32_t                save_restore_addr_hi; /* bits [63:32] of FB address of GART memory used as save/restore buffer */
    uint32_t                buf_size;             /* Size of the save/restore buffer in bytes */
    enum psp_gfx_fw_type    fw_type;              /* FW type */
};

/* All GFX ring buffer commands. */
union psp_gfx_commands
{
    struct psp_gfx_cmd_load_ta          cmd_load_ta;
    struct psp_gfx_cmd_unload_ta        cmd_unload_ta;
    struct psp_gfx_cmd_invoke_cmd       cmd_invoke_cmd;
    struct psp_gfx_cmd_setup_tmr        cmd_setup_tmr;
    struct psp_gfx_cmd_load_ip_fw       cmd_load_ip_fw;
    struct psp_gfx_cmd_save_restore_ip_fw cmd_save_restore_ip_fw;
};


/* Structure of GFX Response buffer.
* For GPCOM I/F it is part of GFX_CMD_RESP buffer, for RBI
* it is separate buffer.
*/
struct psp_gfx_resp
{
    uint32_t    status;             /* +0  status of command execution */
    uint32_t    session_id;         /* +4  session ID in response to LoadTa command */
    uint32_t    fw_addr_lo;         /* +8  bits [31:0] of FW address within TMR (in response to cmd_load_ip_fw command) */
    uint32_t    fw_addr_hi;         /* +12 bits [63:32] of FW address within TMR (in response to cmd_load_ip_fw command) */

    uint32_t    reserved[4];

    /* total 32 bytes */
};

/* Structure of Command buffer pointed by psp_gfx_rb_frame.cmd_buf_addr_hi
*  and psp_gfx_rb_frame.cmd_buf_addr_lo.
*/
struct psp_gfx_cmd_resp
{
    uint32_t        buf_size;           /* +0  total size of the buffer in bytes */
    uint32_t        buf_version;        /* +4  version of the buffer strusture; must be PSP_GFX_CMD_BUF_VERSION */
    uint32_t        cmd_id;             /* +8  command ID */

    /* These fields are used for RBI only. They are all 0 in GPCOM commands
    */
    uint32_t        resp_buf_addr_lo;   /* +12 bits [31:0] of GPU Virtual address of response buffer (must be 4 KB aligned) */
    uint32_t        resp_buf_addr_hi;   /* +16 bits [63:32] of GPU Virtual address of response buffer */
    uint32_t        resp_offset;        /* +20 offset within response buffer */
    uint32_t        resp_buf_size;      /* +24 total size of the response buffer in bytes */

    union psp_gfx_commands  cmd;        /* +28 command specific structures */

    uint8_t         reserved_1[864 - sizeof(union psp_gfx_commands) - 28];

    /* Note: Resp is part of this buffer for GPCOM ring. For RBI ring the response
    *        is separate buffer pointed by resp_buf_addr_hi and resp_buf_addr_lo.
    */
    struct psp_gfx_resp     resp;       /* +864 response */

    uint8_t         reserved_2[1024 - 864 - sizeof(struct psp_gfx_resp)];

    /* total size 1024 bytes */
};


#define FRAME_TYPE_DESTROY          1   /* frame sent by KMD driver when UMD Scheduler context is destroyed*/

/* Structure of the Ring Buffer Frame */
struct psp_gfx_rb_frame
{
    uint32_t    cmd_buf_addr_lo;    /* +0  bits [31:0] of GPU Virtual address of command buffer (must be 4 KB aligned) */
    uint32_t    cmd_buf_addr_hi;    /* +4  bits [63:32] of GPU Virtual address of command buffer */
    uint32_t    cmd_buf_size;       /* +8  command buffer size in bytes */
    uint32_t    fence_addr_lo;      /* +12 bits [31:0] of GPU Virtual address of Fence for this frame */
    uint32_t    fence_addr_hi;      /* +16 bits [63:32] of GPU Virtual address of Fence for this frame */
    uint32_t    fence_value;        /* +20 Fence value */
    uint32_t    sid_lo;             /* +24 bits [31:0] of SID value (used only for RBI frames) */
    uint32_t    sid_hi;             /* +28 bits [63:32] of SID value (used only for RBI frames) */
    uint8_t     vmid;               /* +32 VMID value used for mapping of all addresses for this frame */
    uint8_t     frame_type;         /* +33 1: destory context frame, 0: all other frames; used only for RBI frames */
    uint8_t     reserved1[2];       /* +34 reserved, must be 0 */
    uint32_t    reserved2[7];       /* +36 reserved, must be 0 */
                /* total 64 bytes */
};

#endif /* _PSP_TEE_GFX_IF_H_ */
