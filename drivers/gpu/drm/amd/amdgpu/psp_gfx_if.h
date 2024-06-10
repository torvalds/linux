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

/* USBC PD FW version retrieval command */
#define C2PMSG_CMD_GFX_USB_PD_FW_VER 0x2000000

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
    GFX_CTRL_CMD_ID_GBR_IH_SET      = 0x00080000,   /* set Gbr IH_RB_CNTL registers */
    GFX_CTRL_CMD_ID_CONSUME_CMD     = 0x00090000,   /* send interrupt to psp for updating write pointer of vf */
    GFX_CTRL_CMD_ID_DESTROY_GPCOM_RING = 0x000C0000, /* destroy GPCOM ring */

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
    GFX_CMD_ID_LOAD_TA            = 0x00000001,   /* load TA */
    GFX_CMD_ID_UNLOAD_TA          = 0x00000002,   /* unload TA */
    GFX_CMD_ID_INVOKE_CMD         = 0x00000003,   /* send command to TA */
    GFX_CMD_ID_LOAD_ASD           = 0x00000004,   /* load ASD Driver */
    GFX_CMD_ID_SETUP_TMR          = 0x00000005,   /* setup TMR region */
    GFX_CMD_ID_LOAD_IP_FW         = 0x00000006,   /* load HW IP FW */
    GFX_CMD_ID_DESTROY_TMR        = 0x00000007,   /* destroy TMR region */
    GFX_CMD_ID_SAVE_RESTORE       = 0x00000008,   /* save/restore HW IP FW */
    GFX_CMD_ID_SETUP_VMR          = 0x00000009,   /* setup VMR region */
    GFX_CMD_ID_DESTROY_VMR        = 0x0000000A,   /* destroy VMR region */
    GFX_CMD_ID_PROG_REG           = 0x0000000B,   /* program regs */
    GFX_CMD_ID_GET_FW_ATTESTATION = 0x0000000F,   /* Query GPUVA of the Fw Attestation DB */
    /* IDs upto 0x1F are reserved for older programs (Raven, Vega 10/12/20) */
    GFX_CMD_ID_LOAD_TOC           = 0x00000020,   /* Load TOC and obtain TMR size */
    GFX_CMD_ID_AUTOLOAD_RLC       = 0x00000021,   /* Indicates all graphics fw loaded, start RLC autoload */
    GFX_CMD_ID_BOOT_CFG           = 0x00000022,   /* Boot Config */
    GFX_CMD_ID_SRIOV_SPATIAL_PART = 0x00000027,   /* Configure spatial partitioning mode */
};

/* PSP boot config sub-commands */
enum psp_gfx_boot_config_cmd
{
    BOOTCFG_CMD_SET         = 1, /* Set boot configuration settings */
    BOOTCFG_CMD_GET         = 2, /* Get boot configuration settings */
    BOOTCFG_CMD_INVALIDATE  = 3  /* Reset current boot configuration settings to VBIOS defaults */
};

/* PSP boot config bitmask values */
enum psp_gfx_boot_config
{
    BOOT_CONFIG_GECC = 0x1,
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
    union {
	struct {
		uint32_t	sriov_enabled:1; /* whether the device runs under SR-IOV*/
		uint32_t	virt_phy_addr:1; /* driver passes both virtual and physical address to PSP*/
		uint32_t	reserved:30;
	} bitfield;
	uint32_t        tmr_flags;
    };
    uint32_t        system_phy_addr_lo;        /* bits [31:0] of system physical address of TMR buffer (must be 4 KB aligned) */
    uint32_t        system_phy_addr_hi;        /* bits [63:32] of system physical address of TMR buffer */

};

/* FW types for GFX_CMD_ID_LOAD_IP_FW command. Limit 31. */
enum psp_gfx_fw_type {
	GFX_FW_TYPE_NONE        = 0,    /* */
	GFX_FW_TYPE_CP_ME       = 1,    /* CP-ME                    VG + RV */
	GFX_FW_TYPE_CP_PFP      = 2,    /* CP-PFP                   VG + RV */
	GFX_FW_TYPE_CP_CE       = 3,    /* CP-CE                    VG + RV */
	GFX_FW_TYPE_CP_MEC      = 4,    /* CP-MEC FW                VG + RV */
	GFX_FW_TYPE_CP_MEC_ME1  = 5,    /* CP-MEC Jump Table 1      VG + RV */
	GFX_FW_TYPE_CP_MEC_ME2  = 6,    /* CP-MEC Jump Table 2      VG      */
	GFX_FW_TYPE_RLC_V       = 7,    /* RLC-V                    VG      */
	GFX_FW_TYPE_RLC_G       = 8,    /* RLC-G                    VG + RV */
	GFX_FW_TYPE_SDMA0       = 9,    /* SDMA0                    VG + RV */
	GFX_FW_TYPE_SDMA1       = 10,   /* SDMA1                    VG      */
	GFX_FW_TYPE_DMCU_ERAM   = 11,   /* DMCU-ERAM                VG + RV */
	GFX_FW_TYPE_DMCU_ISR    = 12,   /* DMCU-ISR                 VG + RV */
	GFX_FW_TYPE_VCN         = 13,   /* VCN                           RV */
	GFX_FW_TYPE_UVD         = 14,   /* UVD                      VG      */
	GFX_FW_TYPE_VCE         = 15,   /* VCE                      VG      */
	GFX_FW_TYPE_ISP         = 16,   /* ISP                           RV */
	GFX_FW_TYPE_ACP         = 17,   /* ACP                           RV */
	GFX_FW_TYPE_SMU         = 18,   /* SMU                      VG      */
	GFX_FW_TYPE_MMSCH       = 19,   /* MMSCH                    VG      */
	GFX_FW_TYPE_RLC_RESTORE_LIST_GPM_MEM        = 20,   /* RLC GPM                  VG + RV */
	GFX_FW_TYPE_RLC_RESTORE_LIST_SRM_MEM        = 21,   /* RLC SRM                  VG + RV */
	GFX_FW_TYPE_RLC_RESTORE_LIST_SRM_CNTL       = 22,   /* RLC CNTL                 VG + RV */
	GFX_FW_TYPE_UVD1        = 23,   /* UVD1                     VG-20   */
	GFX_FW_TYPE_TOC         = 24,   /* TOC                      NV-10   */
	GFX_FW_TYPE_RLC_P                           = 25,   /* RLC P                    NV      */
	GFX_FW_TYPE_RLC_IRAM                        = 26,   /* RLC_IRAM                 NV      */
	GFX_FW_TYPE_GLOBAL_TAP_DELAYS               = 27,   /* GLOBAL TAP DELAYS        NV      */
	GFX_FW_TYPE_SE0_TAP_DELAYS                  = 28,   /* SE0 TAP DELAYS           NV      */
	GFX_FW_TYPE_SE1_TAP_DELAYS                  = 29,   /* SE1 TAP DELAYS           NV      */
	GFX_FW_TYPE_GLOBAL_SE0_SE1_SKEW_DELAYS      = 30,   /* GLOBAL SE0/1 SKEW DELAYS NV      */
	GFX_FW_TYPE_SDMA0_JT                        = 31,   /* SDMA0 JT                 NV      */
	GFX_FW_TYPE_SDMA1_JT                        = 32,   /* SDNA1 JT                 NV      */
	GFX_FW_TYPE_CP_MES                          = 33,   /* CP MES                   NV      */
	GFX_FW_TYPE_MES_STACK                       = 34,   /* MES STACK                NV      */
	GFX_FW_TYPE_RLC_SRM_DRAM_SR                 = 35,   /* RLC SRM DRAM             NV      */
	GFX_FW_TYPE_RLCG_SCRATCH_SR                 = 36,   /* RLCG SCRATCH             NV      */
	GFX_FW_TYPE_RLCP_SCRATCH_SR                 = 37,   /* RLCP SCRATCH             NV      */
	GFX_FW_TYPE_RLCV_SCRATCH_SR                 = 38,   /* RLCV SCRATCH             NV      */
	GFX_FW_TYPE_RLX6_DRAM_SR                    = 39,   /* RLX6 DRAM                NV      */
	GFX_FW_TYPE_SDMA0_PG_CONTEXT                = 40,   /* SDMA0 PG CONTEXT         NV      */
	GFX_FW_TYPE_SDMA1_PG_CONTEXT                = 41,   /* SDMA1 PG CONTEXT         NV      */
	GFX_FW_TYPE_GLOBAL_MUX_SELECT_RAM           = 42,   /* GLOBAL MUX SEL RAM       NV      */
	GFX_FW_TYPE_SE0_MUX_SELECT_RAM              = 43,   /* SE0 MUX SEL RAM          NV      */
	GFX_FW_TYPE_SE1_MUX_SELECT_RAM              = 44,   /* SE1 MUX SEL RAM          NV      */
	GFX_FW_TYPE_ACCUM_CTRL_RAM                  = 45,   /* ACCUM CTRL RAM           NV      */
	GFX_FW_TYPE_RLCP_CAM                        = 46,   /* RLCP CAM                 NV      */
	GFX_FW_TYPE_RLC_SPP_CAM_EXT                 = 47,   /* RLC SPP CAM EXT          NV      */
	GFX_FW_TYPE_RLC_DRAM_BOOT                   = 48,   /* RLC DRAM BOOT            NV      */
	GFX_FW_TYPE_VCN0_RAM                        = 49,   /* VCN_RAM                  NV + RN */
	GFX_FW_TYPE_VCN1_RAM                        = 50,   /* VCN_RAM                  NV + RN */
	GFX_FW_TYPE_DMUB                            = 51,   /* DMUB                          RN */
	GFX_FW_TYPE_SDMA2                           = 52,   /* SDMA2                    MI      */
	GFX_FW_TYPE_SDMA3                           = 53,   /* SDMA3                    MI      */
	GFX_FW_TYPE_SDMA4                           = 54,   /* SDMA4                    MI      */
	GFX_FW_TYPE_SDMA5                           = 55,   /* SDMA5                    MI      */
	GFX_FW_TYPE_SDMA6                           = 56,   /* SDMA6                    MI      */
	GFX_FW_TYPE_SDMA7                           = 57,   /* SDMA7                    MI      */
	GFX_FW_TYPE_VCN1                            = 58,   /* VCN1                     MI      */
	GFX_FW_TYPE_CAP                             = 62,   /* CAP_FW                           */
	GFX_FW_TYPE_SE2_TAP_DELAYS                  = 65,   /* SE2 TAP DELAYS           NV      */
	GFX_FW_TYPE_SE3_TAP_DELAYS                  = 66,   /* SE3 TAP DELAYS           NV      */
	GFX_FW_TYPE_REG_LIST                        = 67,   /* REG_LIST                 MI      */
	GFX_FW_TYPE_IMU_I                           = 68,   /* IMU Instruction FW       SOC21   */
	GFX_FW_TYPE_IMU_D                           = 69,   /* IMU Data FW              SOC21   */
	GFX_FW_TYPE_LSDMA                           = 70,   /* LSDMA FW                 SOC21   */
	GFX_FW_TYPE_SDMA_UCODE_TH0                  = 71,   /* SDMA Thread 0/CTX        SOC21   */
	GFX_FW_TYPE_SDMA_UCODE_TH1                  = 72,   /* SDMA Thread 1/CTL        SOC21   */
	GFX_FW_TYPE_PPTABLE                         = 73,   /* PPTABLE                  SOC21   */
	GFX_FW_TYPE_DISCRETE_USB4                   = 74,   /* dUSB4 FW                 SOC21   */
	GFX_FW_TYPE_TA                              = 75,   /* SRIOV TA FW UUID         SOC21   */
	GFX_FW_TYPE_RS64_MES                        = 76,   /* RS64 MES ucode           SOC21   */
	GFX_FW_TYPE_RS64_MES_STACK                  = 77,   /* RS64 MES stack ucode     SOC21   */
	GFX_FW_TYPE_RS64_KIQ                        = 78,   /* RS64 KIQ ucode           SOC21   */
	GFX_FW_TYPE_RS64_KIQ_STACK                  = 79,   /* RS64 KIQ Heap stack      SOC21   */
	GFX_FW_TYPE_ISP_DATA                        = 80,   /* ISP DATA                 SOC21   */
	GFX_FW_TYPE_CP_MES_KIQ                      = 81,   /* MES KIQ ucode            SOC21   */
	GFX_FW_TYPE_MES_KIQ_STACK                   = 82,   /* MES KIQ stack            SOC21   */
	GFX_FW_TYPE_UMSCH_DATA                      = 83,   /* User Mode Scheduler Data SOC21   */
	GFX_FW_TYPE_UMSCH_UCODE                     = 84,   /* User Mode Scheduler Ucode SOC21  */
	GFX_FW_TYPE_UMSCH_CMD_BUFFER                = 85,   /* User Mode Scheduler Command Buffer SOC21 */
	GFX_FW_TYPE_USB_DP_COMBO_PHY                = 86,   /* USB-Display port Combo   SOC21   */
	GFX_FW_TYPE_RS64_PFP                        = 87,   /* RS64 PFP                 SOC21   */
	GFX_FW_TYPE_RS64_ME                         = 88,   /* RS64 ME                  SOC21   */
	GFX_FW_TYPE_RS64_MEC                        = 89,   /* RS64 MEC                 SOC21   */
	GFX_FW_TYPE_RS64_PFP_P0_STACK               = 90,   /* RS64 PFP stack P0        SOC21   */
	GFX_FW_TYPE_RS64_PFP_P1_STACK               = 91,   /* RS64 PFP stack P1        SOC21   */
	GFX_FW_TYPE_RS64_ME_P0_STACK                = 92,   /* RS64 ME stack P0         SOC21   */
	GFX_FW_TYPE_RS64_ME_P1_STACK                = 93,   /* RS64 ME stack P1         SOC21   */
	GFX_FW_TYPE_RS64_MEC_P0_STACK               = 94,   /* RS64 MEC stack P0        SOC21   */
	GFX_FW_TYPE_RS64_MEC_P1_STACK               = 95,   /* RS64 MEC stack P1        SOC21   */
	GFX_FW_TYPE_RS64_MEC_P2_STACK               = 96,   /* RS64 MEC stack P2        SOC21   */
	GFX_FW_TYPE_RS64_MEC_P3_STACK               = 97,   /* RS64 MEC stack P3        SOC21   */
	GFX_FW_TYPE_VPEC_FW1                        = 100,  /* VPEC FW1 To Save         VPE     */
	GFX_FW_TYPE_VPEC_FW2                        = 101,  /* VPEC FW2 To Save         VPE     */
	GFX_FW_TYPE_VPE                             = 102,
	GFX_FW_TYPE_JPEG_RAM                        = 128,  /**< JPEG Command buffer */
	GFX_FW_TYPE_P2S_TABLE                       = 129,
	GFX_FW_TYPE_MAX
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

/* Command to setup register program */
struct psp_gfx_cmd_reg_prog {
	uint32_t	reg_value;
	uint32_t	reg_id;
};

/* Command to load TOC */
struct psp_gfx_cmd_load_toc
{
    uint32_t        toc_phy_addr_lo;        /* bits [31:0] of GPU Virtual address of FW location (must be 4 KB aligned) */
    uint32_t        toc_phy_addr_hi;        /* bits [63:32] of GPU Virtual address of FW location */
    uint32_t        toc_size;               /* FW buffer size in bytes */
};

/* Dynamic boot configuration */
struct psp_gfx_cmd_boot_cfg
{
    uint32_t                        timestamp;            /* calendar time as number of seconds */
    enum psp_gfx_boot_config_cmd    sub_cmd;              /* sub-command indicating how to process command data */
    uint32_t                        boot_config;          /* dynamic boot configuration bitmask */
    uint32_t                        boot_config_valid;    /* dynamic boot configuration valid bits bitmask */
};

struct psp_gfx_cmd_sriov_spatial_part {
	uint32_t mode;
	uint32_t override_ips;
	uint32_t override_xcds_avail;
	uint32_t override_this_aid;
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
    struct psp_gfx_cmd_reg_prog       cmd_setup_reg_prog;
    struct psp_gfx_cmd_setup_tmr        cmd_setup_vmr;
    struct psp_gfx_cmd_load_toc         cmd_load_toc;
    struct psp_gfx_cmd_boot_cfg         boot_cfg;
    struct psp_gfx_cmd_sriov_spatial_part cmd_spatial_part;
};

struct psp_gfx_uresp_reserved
{
    uint32_t reserved[8];
};

/* Command-specific response for Fw Attestation Db */
struct psp_gfx_uresp_fwar_db_info
{
    uint32_t fwar_db_addr_lo;
    uint32_t fwar_db_addr_hi;
};

/* Command-specific response for boot config. */
struct psp_gfx_uresp_bootcfg {
	uint32_t boot_cfg;	/* boot config data */
};

/* Union of command-specific responses for GPCOM ring. */
union psp_gfx_uresp {
	struct psp_gfx_uresp_reserved		reserved;
	struct psp_gfx_uresp_bootcfg		boot_cfg;
	struct psp_gfx_uresp_fwar_db_info	fwar_db_info;
};

/* Structure of GFX Response buffer.
* For GPCOM I/F it is part of GFX_CMD_RESP buffer, for RBI
* it is separate buffer.
*/
struct psp_gfx_resp
{
    uint32_t	status;		/* +0  status of command execution */
    uint32_t	session_id;	/* +4  session ID in response to LoadTa command */
    uint32_t	fw_addr_lo;	/* +8  bits [31:0] of FW address within TMR (in response to cmd_load_ip_fw command) */
    uint32_t	fw_addr_hi;	/* +12 bits [63:32] of FW address within TMR (in response to cmd_load_ip_fw command) */
    uint32_t	tmr_size;	/* +16 size of the TMR to be reserved including MM fw and Gfx fw in response to cmd_load_toc command */

    uint32_t	reserved[11];

    union psp_gfx_uresp uresp;      /* +64 response union containing command-specific responses */

    /* total 96 bytes */
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

#define PSP_ERR_UNKNOWN_COMMAND 0x00000100

enum tee_error_code {
    TEE_SUCCESS                         = 0x00000000,
    TEE_ERROR_NOT_SUPPORTED             = 0xFFFF000A,
};

#endif /* _PSP_TEE_GFX_IF_H_ */
