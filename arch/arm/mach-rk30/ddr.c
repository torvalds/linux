#include <linux/kernel.h>
#include <linux/io.h>
#include <mach/sram.h>

typedef uint32_t uint32;
#define PMU_BASE_ADDR           RK30_PMU_BASE
#define SDRAMC_BASE_ADDR        RK30_DDR_PCTL_BASE
#define DDR_PUBL_BASE           RK30_DDR_PUBL_BASE
#define CRU_BASE_ADDR           RK30_CRU_BASE
#define Delay_us(usecs)		sram_udelay(usecs)


//PMU_MISC_CON1
#define idle_req_cpu_cfg    (1<<1)
#define idle_req_peri_cfg   (1<<2)
#define idle_req_gpu_cfg    (1<<3)
#define idle_req_video_cfg  (1<<4)
#define idle_req_vio_cfg    (1<<5)

//PMU_PWRDN_ST
#define idle_cpu    (1<<26)
#define idle_peri   (1<<25)
#define idle_gpu    (1<<24)
#define idle_video  (1<<23)
#define idle_vio    (1<<22)

//PMU registers
typedef volatile struct tagPMU_FILE
{
    uint32 PMU_WAKEUP_CFG[2];
    uint32 PMU_PWRDN_CON;
    uint32 PMU_PWRDN_ST;
    uint32 PMU_INT_CON;
    uint32 PMU_INT_ST;
    uint32 PMU_MISC_CON;
    uint32 PMU_OSC_CNT;
    uint32 PMU_PLL_CNT;
    uint32 PMU_PMU_CNT;
    uint32 PMU_DDRIO_PWRON_CNT;
    uint32 PMU_WAKEUP_RST_CLR_CNT;
    uint32 PMU_SCU_PWRDWN_CNT;
    uint32 PMU_SCU_PWRUP_CNT;
    uint32 PMU_MISC_CON1;
    uint32 PMU_GPIO6_CON;
    uint32 PMU_PMU_SYS_REG[4];
} PMU_FILE, *pPMU_FILE;

#define pPMU_Reg ((pPMU_FILE)PMU_BASE_ADDR)

 //CRU Registers
typedef volatile struct tagCRU_STRUCT 
{
    uint32 CRU_PLL_CON[4][4]; 
    uint32 CRU_MODE_CON;
    uint32 CRU_CLKSEL_CON[35];
    uint32 CRU_CLKGATE_CON[10];
    uint32 reserved1[2];
    uint32 CRU_GLB_SRST_FST_VALUE;
    uint32 CRU_GLB_SRST_SND_VALUE;
    uint32 reserved2[2];
    uint32 CRU_SOFTRST_CON[9];
    uint32 CRU_MISC_CON;
    uint32 reserved3[2];
    uint32 CRU_GLB_CNT_TH;
} CRU_REG, *pCRU_REG;

#define pCRU_Reg ((pCRU_REG)CRU_BASE_ADDR)


//SCTL
#define INIT_STATE                     (0)
#define CFG_STATE                      (1)
#define GO_STATE                       (2)
#define SLEEP_STATE                    (3)
#define WAKEUP_STATE                   (4)

//STAT
#define Init_mem                       (0)
#define Config                         (1)
#define Config_req                     (2)
#define Access                         (3)
#define Access_req                     (4)
#define Low_power                      (5)
#define Low_power_entry_req            (6)
#define Low_power_exit_req             (7)

//MCFG
#define mddr_lpddr2_clk_stop_idle(n)   (n<<24)
#define pd_idle(n)                     (n<<8)
#define mddr_en                        (2<<22)
#define lpddr2_en                      (3<<22)
#define ddr2_en                        (0<<5)
#define ddr3_en                        (1<<5)
#define lpddr2_s2                      (0<<6)
#define lpddr2_s4                      (1<<6)
#define mddr_lpddr2_bl_2               (0<<20)
#define mddr_lpddr2_bl_4               (1<<20)
#define mddr_lpddr2_bl_8               (2<<20)
#define mddr_lpddr2_bl_16              (3<<20)
#define ddr2_ddr3_bl_4                 (0)
#define ddr2_ddr3_bl_8                 (1)
#define tfaw_cfg(n)                    ((n-4)<<18)
#define pd_exit_slow                   (0<<17)
#define pd_exit_fast                   (1<<17)
#define pd_type(n)                     (n<<16)
#define two_t_en(n)                    (n<<3)
#define bl8int_en(n)                   (n<<2)
#define cke_or_en(n)                   (n<<1)

//POWCTL
#define power_up_start                 (1<<0)

//POWSTAT
#define power_up_done                  (1<<0)

//DFISTSTAT0
#define dfi_init_complete              (1<<0)

//CMDTSTAT
#define cmd_tstat                      (1<<0)

//CMDTSTATEN
#define cmd_tstat_en                   (1<<1)

//MCMD
#define Deselect_cmd                   (0)
#define PREA_cmd                       (1)
#define REF_cmd                        (2)
#define MRS_cmd                        (3)
#define ZQCS_cmd                       (4)
#define ZQCL_cmd                       (5)
#define RSTL_cmd                       (6)
#define MRR_cmd                        (8)
#define DPDE_cmd                       (9)

#define lpddr2_op(n)                   (n<<12)
#define lpddr2_ma(n)                   (n<<4)

#define bank_addr(n)                   (n<<17)
#define cmd_addr(n)                    (n<<4)

#define start_cmd                      (1u<<31)

typedef union STAT_Tag
{
    uint32 d32;
    struct
    {
        unsigned ctl_stat : 3;
        unsigned reserved3 : 1;
        unsigned lp_trig : 3;
        unsigned reserved7_31 : 25;
    }b;
}STAT_T;

typedef union SCFG_Tag
{
    uint32 d32;
    struct 
    {
        unsigned hw_low_power_en : 1;
        unsigned reserved1_5 : 5;
        unsigned nfifo_nif1_dis : 1;
        unsigned reserved7 : 1;
        unsigned bbflags_timing : 4;
        unsigned reserved12_31 : 20;
    } b;
}SCFG_T;

/* DDR Controller register struct */
typedef volatile struct DDR_REG_Tag
{
    //Operational State, Control, and Status Registers
    SCFG_T SCFG;                   //State Configuration Register
    volatile uint32 SCTL;                   //State Control Register
    STAT_T STAT;                   //State Status Register
    volatile uint32 INTRSTAT;               //Interrupt Status Register
    uint32 reserved0[(0x40-0x10)/4];
    //Initailization Control and Status Registers
    volatile uint32 MCMD;                   //Memory Command Register
    volatile uint32 POWCTL;                 //Power Up Control Registers
    volatile uint32 POWSTAT;                //Power Up Status Register
    volatile uint32 CMDTSTAT;               //Command Timing Status Register
    volatile uint32 CMDTSTATEN;             //Command Timing Status Enable Register
    uint32 reserved1[(0x60-0x54)/4];
    volatile uint32 MRRCFG0;                //MRR Configuration 0 Register
    volatile uint32 MRRSTAT0;               //MRR Status 0 Register
    volatile uint32 MRRSTAT1;               //MRR Status 1 Register
    uint32 reserved2[(0x7c-0x6c)/4];
    //Memory Control and Status Registers
    volatile uint32 MCFG1;                  //Memory Configuration 1 Register
    volatile uint32 MCFG;                   //Memory Configuration Register
    volatile uint32 PPCFG;                  //Partially Populated Memories Configuration Register
    volatile uint32 MSTAT;                  //Memory Status Register
    volatile uint32 LPDDR2ZQCFG;            //LPDDR2 ZQ Configuration Register
    uint32 reserved3;
    //DTU Control and Status Registers
    volatile uint32 DTUPDES;                //DTU Status Register
    volatile uint32 DTUNA;                  //DTU Number of Random Addresses Created Register
    volatile uint32 DTUNE;                  //DTU Number of Errors Register
    volatile uint32 DTUPRD0;                //DTU Parallel Read 0
    volatile uint32 DTUPRD1;                //DTU Parallel Read 1
    volatile uint32 DTUPRD2;                //DTU Parallel Read 2
    volatile uint32 DTUPRD3;                //DTU Parallel Read 3
    volatile uint32 DTUAWDT;                //DTU Address Width
    uint32 reserved4[(0xc0-0xb4)/4];
    //Memory Timing Registers
    volatile uint32 TOGCNT1U;               //Toggle Counter 1U Register
    volatile uint32 TINIT;                  //t_init Timing Register
    volatile uint32 TRSTH;                  //Reset High Time Register
    volatile uint32 TOGCNT100N;             //Toggle Counter 100N Register
    volatile uint32 TREFI;                  //t_refi Timing Register
    volatile uint32 TMRD;                   //t_mrd Timing Register
    volatile uint32 TRFC;                   //t_rfc Timing Register
    volatile uint32 TRP;                    //t_rp Timing Register
    volatile uint32 TRTW;                   //t_rtw Timing Register
    volatile uint32 TAL;                    //AL Latency Register
    volatile uint32 TCL;                    //CL Timing Register
    volatile uint32 TCWL;                   //CWL Register
    volatile uint32 TRAS;                   //t_ras Timing Register
    volatile uint32 TRC;                    //t_rc Timing Register
    volatile uint32 TRCD;                   //t_rcd Timing Register
    volatile uint32 TRRD;                   //t_rrd Timing Register
    volatile uint32 TRTP;                   //t_rtp Timing Register
    volatile uint32 TWR;                    //t_wr Timing Register
    volatile uint32 TWTR;                   //t_wtr Timing Register
    volatile uint32 TEXSR;                  //t_exsr Timing Register
    volatile uint32 TXP;                    //t_xp Timing Register
    volatile uint32 TXPDLL;                 //t_xpdll Timing Register
    volatile uint32 TZQCS;                  //t_zqcs Timing Register
    volatile uint32 TZQCSI;                 //t_zqcsi Timing Register
    volatile uint32 TDQS;                   //t_dqs Timing Register
    volatile uint32 TCKSRE;                 //t_cksre Timing Register
    volatile uint32 TCKSRX;                 //t_cksrx Timing Register
    volatile uint32 TCKE;                   //t_cke Timing Register
    volatile uint32 TMOD;                   //t_mod Timing Register
    volatile uint32 TRSTL;                  //Reset Low Timing Register
    volatile uint32 TZQCL;                  //t_zqcl Timing Register
    volatile uint32 TMRR;                   //t_mrr Timing Register
    volatile uint32 TCKESR;                 //t_ckesr Timing Register
    volatile uint32 TDPD;                   //t_dpd Timing Register
    uint32 reserved5[(0x180-0x148)/4];
    //ECC Configuration, Control, and Status Registers
    volatile uint32 ECCCFG;                   //ECC Configuration Register
    volatile uint32 ECCTST;                   //ECC Test Register
    volatile uint32 ECCCLR;                   //ECC Clear Register
    volatile uint32 ECCLOG;                   //ECC Log Register
    uint32 reserved6[(0x200-0x190)/4];
    //DTU Control and Status Registers
    volatile uint32 DTUWACTL;                 //DTU Write Address Control Register
    volatile uint32 DTURACTL;                 //DTU Read Address Control Register
    volatile uint32 DTUCFG;                   //DTU Configuration Control Register
    volatile uint32 DTUECTL;                  //DTU Execute Control Register
    volatile uint32 DTUWD0;                   //DTU Write Data 0
    volatile uint32 DTUWD1;                   //DTU Write Data 1
    volatile uint32 DTUWD2;                   //DTU Write Data 2
    volatile uint32 DTUWD3;                   //DTU Write Data 3
    volatile uint32 DTUWDM;                   //DTU Write Data Mask
    volatile uint32 DTURD0;                   //DTU Read Data 0
    volatile uint32 DTURD1;                   //DTU Read Data 1
    volatile uint32 DTURD2;                   //DTU Read Data 2
    volatile uint32 DTURD3;                   //DTU Read Data 3
    volatile uint32 DTULFSRWD;                //DTU LFSR Seed for Write Data Generation
    volatile uint32 DTULFSRRD;                //DTU LFSR Seed for Read Data Generation
    volatile uint32 DTUEAF;                   //DTU Error Address FIFO
    //DFI Control Registers
    volatile uint32 DFITCTRLDELAY;            //DFI tctrl_delay Register
    volatile uint32 DFIODTCFG;                //DFI ODT Configuration Register
    volatile uint32 DFIODTCFG1;               //DFI ODT Configuration 1 Register
    volatile uint32 DFIODTRANKMAP;            //DFI ODT Rank Mapping Register
    //DFI Write Data Registers
    volatile uint32 DFITPHYWRDATA;            //DFI tphy_wrdata Register
    volatile uint32 DFITPHYWRLAT;             //DFI tphy_wrlat Register
    uint32 reserved7[(0x260-0x258)/4];
    volatile uint32 DFITRDDATAEN;             //DFI trddata_en Register
    volatile uint32 DFITPHYRDLAT;             //DFI tphy_rddata Register
    uint32 reserved8[(0x270-0x268)/4];
    //DFI Update Registers
    volatile uint32 DFITPHYUPDTYPE0;          //DFI tphyupd_type0 Register
    volatile uint32 DFITPHYUPDTYPE1;          //DFI tphyupd_type1 Register
    volatile uint32 DFITPHYUPDTYPE2;          //DFI tphyupd_type2 Register
    volatile uint32 DFITPHYUPDTYPE3;          //DFI tphyupd_type3 Register
    volatile uint32 DFITCTRLUPDMIN;           //DFI tctrlupd_min Register
    volatile uint32 DFITCTRLUPDMAX;           //DFI tctrlupd_max Register
    volatile uint32 DFITCTRLUPDDLY;           //DFI tctrlupd_dly Register
    uint32 reserved9;
    volatile uint32 DFIUPDCFG;                //DFI Update Configuration Register
    volatile uint32 DFITREFMSKI;              //DFI Masked Refresh Interval Register
    volatile uint32 DFITCTRLUPDI;             //DFI tctrlupd_interval Register
    uint32 reserved10[(0x2ac-0x29c)/4];
    volatile uint32 DFITRCFG0;                //DFI Training Configuration 0 Register
    volatile uint32 DFITRSTAT0;               //DFI Training Status 0 Register
    volatile uint32 DFITRWRLVLEN;             //DFI Training dfi_wrlvl_en Register
    volatile uint32 DFITRRDLVLEN;             //DFI Training dfi_rdlvl_en Register
    volatile uint32 DFITRRDLVLGATEEN;         //DFI Training dfi_rdlvl_gate_en Register
    //DFI Status Registers
    volatile uint32 DFISTSTAT0;               //DFI Status Status 0 Register
    volatile uint32 DFISTCFG0;                //DFI Status Configuration 0 Register
    volatile uint32 DFISTCFG1;                //DFI Status configuration 1 Register
    uint32 reserved11;
    volatile uint32 DFITDRAMCLKEN;            //DFI tdram_clk_enalbe Register
    volatile uint32 DFITDRAMCLKDIS;           //DFI tdram_clk_disalbe Register
    volatile uint32 DFISTCFG2;                //DFI Status configuration 2 Register
    volatile uint32 DFISTPARCLR;              //DFI Status Parity Clear Register
    volatile uint32 DFISTPARLOG;              //DFI Status Parity Log Register
    uint32 reserved12[(0x2f0-0x2e4)/4];
    //DFI Low Power Registers
    volatile uint32 DFILPCFG0;                //DFI Low Power Configuration 0 Register
    uint32 reserved13[(0x300-0x2f4)/4];
    //DFI Training 2 Registers
    volatile uint32 DFITRWRLVLRESP0;          //DFI Training dif_wrlvl_resp Status 0 Register
    volatile uint32 DFITRWRLVLRESP1;          //DFI Training dif_wrlvl_resp Status 1 Register
    volatile uint32 DFITRWRLVLRESP2;          //DFI Training dif_wrlvl_resp Status 2 Register
    volatile uint32 DFITRRDLVLRESP0;          //DFI Training dif_rdlvl_resp Status 0 Register
    volatile uint32 DFITRRDLVLRESP1;          //DFI Training dif_rdlvl_resp Status 1 Register
    volatile uint32 DFITRRDLVLRESP2;          //DFI Training dif_rdlvl_resp Status 2 Register
    volatile uint32 DFITRWRLVLDELAY0;         //DFI Training dif_wrlvl_delay Configuration 0 Register
    volatile uint32 DFITRWRLVLDELAY1;         //DFI Training dif_wrlvl_delay Configuration 1 Register
    volatile uint32 DFITRWRLVLDELAY2;         //DFI Training dif_wrlvl_delay Configuration 2 Register
    volatile uint32 DFITRRDLVLDELAY0;         //DFI Training dif_rdlvl_delay Configuration 0 Register
    volatile uint32 DFITRRDLVLDELAY1;         //DFI Training dif_rdlvl_delay Configuration 1 Register
    volatile uint32 DFITRRDLVLDELAY2;         //DFI Training dif_rdlvl_delay Configuration 2 Register
    volatile uint32 DFITRRDLVLGATEDELAY0;     //DFI Training dif_rdlvl_gate_delay Configuration 0 Register
    volatile uint32 DFITRRDLVLGATEDELAY1;     //DFI Training dif_rdlvl_gate_delay Configuration 1 Register
    volatile uint32 DFITRRDLVLGATEDELAY2;     //DFI Training dif_rdlvl_gate_delay Configuration 2 Register
    volatile uint32 DFITRCMD;                 //DFI Training Command Register
    uint32 reserved14[(0x3f8-0x340)/4];
    //IP Status Registers
    volatile uint32 IPVR;                     //IP Version Register
    volatile uint32 IPTR;                     //IP Type Register
}DDR_REG_T, *pDDR_REG_T;

#define pDDR_Reg ((pDDR_REG_T)SDRAMC_BASE_ADDR)

//PIR
#define INIT                 (1<<0)
#define DLLSRST              (1<<1)
#define DLLLOCK              (1<<2)
#define ZCAL                 (1<<3)
#define ITMSRST              (1<<4)
#define DRAMRST              (1<<5)
#define DRAMINIT             (1<<6)
#define QSTRN                (1<<7)
#define EYETRN               (1<<8)
#define DLLBYP               (1<<17)
#define CTLDINIT             (1<<18)
#define CLRSR                (1<<28)
#define LOCKBYP              (1<<29)
#define ZCALBYP              (1<<30)
#define INITBYP              (1u<<31)

//PGCR
#define DFTLMT(n)            (n<<3)
#define DFTCMP(n)            (n<<2)
#define DQSCFG(n)            (n<<1)
#define ITMDMD(n)            (n<<0)
#define RANKEN(n)            (n<<18)

//PGSR
#define IDONE                (1<<0)
#define DLDONE               (1<<1)
#define ZCDONE               (1<<2)
#define DIDONE               (1<<3)
#define DTDONE               (1<<4)
#define DTERR                (1<<5)
#define DTIERR               (1<<6)
#define DFTERR               (1<<7)
#define TQ                   (1u<<31)

//PTR0
#define tITMSRST(n)          (n<<18)
#define tDLLLOCK(n)          (n<<6)
#define tDLLSRST(n)          (n<<0)

//PTR1
#define tDINIT1(n)           (n<<19)
#define tDINIT0(n)           (n<<0)

//PTR2
#define tDINIT3(n)           (n<<17)
#define tDINIT2(n)           (n<<0)

//DSGCR
#define DQSGE(n)             (n<<8)
#define DQSGX(n)             (n<<5)

typedef union DCR_Tag
{
    uint32 d32;
    struct 
    {
        unsigned DDRMD : 3;
        unsigned DDR8BNK : 1;
        unsigned PDQ : 3;
        unsigned MPRDQ : 1;
        unsigned DDRTYPE : 2;
        unsigned reserved10_26 : 17;
        unsigned NOSRA : 1;
        unsigned DDR2T : 1;
        unsigned UDIMM : 1;
        unsigned RDIMM : 1;
        unsigned TPD : 1;
    } b;
}DCR_T;


typedef volatile struct DATX8_REG_Tag
{
    volatile uint32 DXGCR;                 //DATX8 General Configuration Register
    volatile uint32 DXGSR[2];              //DATX8 General Status Register
    volatile uint32 DXDLLCR;               //DATX8 DLL Control Register
    volatile uint32 DXDQTR;                //DATX8 DQ Timing Register
    volatile uint32 DXDQSTR;               //DATX8 DQS Timing Register
    uint32 reserved[0x80-0x76];
}DATX8_REG_T;

/* DDR PHY register struct */
typedef volatile struct DDRPHY_REG_Tag
{
    volatile uint32 RIDR;                   //Revision Identification Register
    volatile uint32 PIR;                    //PHY Initialization Register
    volatile uint32 PGCR;                   //PHY General Configuration Register
    volatile uint32 PGSR;                   //PHY General Status Register
    volatile uint32 DLLGCR;                 //DLL General Control Register
    volatile uint32 ACDLLCR;                //AC DLL Control Register
    volatile uint32 PTR[3];                 //PHY Timing Registers 0-2
    volatile uint32 ACIOCR;                 //AC I/O Configuration Register
    volatile uint32 DXCCR;                  //DATX8 Common Configuration Register
    volatile uint32 DSGCR;                  //DDR System General Configuration Register
    DCR_T DCR;                    //DRAM Configuration Register
    volatile uint32 DTPR[3];                //DRAM Timing Parameters Register 0-2
    volatile uint32 MR[4];                    //Mode Register 0-3
    volatile uint32 ODTCR;                  //ODT Configuration Register
    volatile uint32 DTAR;                   //Data Training Address Register
    volatile uint32 DTDR[2];                //Data Training Data Register 0-1
    
    uint32 reserved1[0x30-0x18];
    uint32 DCU[0x38-0x30];
    uint32 reserved2[0x40-0x38];
    uint32 BIST[0x51-0x40];
    uint32 reserved3[0x60-0x51];
    
    volatile uint32 ZQ0CR[2];               //ZQ 0 Impedance Control Register 0-1
    volatile uint32 ZQ0SR[2];               //ZQ 0 Impedance Status Register 0-1
    volatile uint32 ZQ1CR[2];               //ZQ 1 Impedance Control Register 0-1
    volatile uint32 ZQ1SR[2];               //ZQ 1 Impedance Status Register 0-1
    volatile uint32 ZQ2CR[2];               //ZQ 2 Impedance Control Register 0-1
    volatile uint32 ZQ2SR[2];               //ZQ 2 Impedance Status Register 0-1
    volatile uint32 ZQ3CR[2];               //ZQ 3 Impedance Control Register 0-1
    volatile uint32 ZQ3SR[2];               //ZQ 3 Impedance Status Register 0-1
    
    DATX8_REG_T     DATX8[9];               //DATX8 Register 
}DDRPHY_REG_T, *pDDRPHY_REG_T;

#define pPHY_Reg ((pDDRPHY_REG_T)DDR_PUBL_BASE)

__sramfunc static void Idle_port(void)
{
    pPMU_Reg->PMU_MISC_CON1 = (pPMU_Reg->PMU_MISC_CON1 & (~(0xF<<2)))
                                        | idle_req_peri_cfg
                                        | idle_req_gpu_cfg
                                        | idle_req_video_cfg
                                        | idle_req_vio_cfg;
    while(((pPMU_Reg->PMU_PWRDN_ST) & (idle_peri
                                        | idle_gpu
                                        | idle_video
                                        | idle_vio)) != (idle_peri
                                        | idle_gpu
                                        | idle_video
                                        | idle_vio));
}

__sramfunc static void DeIdle_port(void)
{
    pPMU_Reg->PMU_MISC_CON1 &= ~(idle_req_peri_cfg
                                 | idle_req_gpu_cfg
                                 | idle_req_video_cfg
                                 | idle_req_vio_cfg);
}

__sramfunc void Move_to_Lowpower_state(void)
{
    volatile uint32 value;

    while(1)
    {
        value = pDDR_Reg->STAT.b.ctl_stat;
        if(value == Low_power)
        {
            break;
        }
        switch(value)

        {
            case Init_mem:
                pDDR_Reg->SCTL = CFG_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Config);
            case Config:
                pDDR_Reg->SCTL = GO_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
            case Access:
                pDDR_Reg->SCTL = SLEEP_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Low_power);
                break;
            default:  //Transitional state
                break;
        }
    }
}

__sramfunc void Move_to_Access_state(void)
{
    volatile uint32 value;

    while(1)
    {
        value = pDDR_Reg->STAT.b.ctl_stat;
        if(value == Access)
        {
            break;
        }
        switch(value)

        {
            case Low_power:
                pDDR_Reg->SCTL = WAKEUP_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
                while((pPHY_Reg->PGSR & DLDONE) != DLDONE);  //wait DLL lock
                break;
            case Init_mem:
                pDDR_Reg->SCTL = CFG_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Config);
            case Config:
                pDDR_Reg->SCTL = GO_STATE;
                while((pDDR_Reg->STAT.b.ctl_stat) != Access);
                break;
            default:  //Transitional state
                break;
        }
    }
}

__sramfunc void ddr_selfrefresh_enter(void)
{
    uint32 i;
    
//    Idle_port();
    Move_to_Lowpower_state();

    pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16) | (1<<2);  //disable DDR PHY clock

    pCRU_Reg->CRU_MODE_CON = (0x3<<((1*4) +  16)) | (0x0<<(1*4));            //PLL slow-mode
    
    pPHY_Reg->ACDLLCR |= 0x80000000;
    pPHY_Reg->DATX8[0].DXDLLCR |= 0x80000000;
    pPHY_Reg->DATX8[1].DXDLLCR |= 0x80000000;
    pPHY_Reg->DATX8[2].DXDLLCR |= 0x80000000;
    pPHY_Reg->DATX8[3].DXDLLCR |= 0x80000000;
    pPHY_Reg->PIR = INIT | DLLBYP;
    for (i = 0; i < 10; i ++) {;}
    while((pPHY_Reg->PGSR & IDONE) != IDONE);
}

__sramfunc void ddr_selfrefresh_exit(void)
{
    uint32 i;
    
    pPHY_Reg->ACDLLCR &= ~0x80000000;
    pPHY_Reg->DATX8[0].DXDLLCR &= ~0x80000000;
    pPHY_Reg->DATX8[1].DXDLLCR &= ~0x80000000;
    pPHY_Reg->DATX8[2].DXDLLCR &= ~0x80000000;
    pPHY_Reg->DATX8[3].DXDLLCR &= ~0x80000000;
    pPHY_Reg->PIR = INIT;
    for (i = 0; i < 10; i ++) {;}
    while((pPHY_Reg->PGSR & IDONE) != IDONE);

    pCRU_Reg->CRU_MODE_CON = (0x3<<((1*4) +  16))  | (0x1<<(1*4));            //PLL normal
    Delay_us(10);   //wait pll lock

    pCRU_Reg->CRU_CLKGATE_CON[0] = ((0x1<<2)<<16) | (0<<2);  //enable DDR PHY clock
    Delay_us(10);   //wait
    
    // start to add by cjh 20120330   
    // reset dll 
    pPHY_Reg->ACDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[0].DXDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[1].DXDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[2].DXDLLCR &= ~0x40000000;
    pPHY_Reg->DATX8[3].DXDLLCR &= ~0x40000000;
    
    Delay_us(10);   //wait
    
     // de-reset dll    
    pPHY_Reg->ACDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[0].DXDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[1].DXDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[2].DXDLLCR |= 0x40000000;
    pPHY_Reg->DATX8[3].DXDLLCR |= 0x40000000;  
    
    Delay_us(10);   //wait
    // end to add by cjh 20120330   
    
    Move_to_Access_state();
//    DeIdle_port();
}

