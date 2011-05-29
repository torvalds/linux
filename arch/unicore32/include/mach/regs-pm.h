/*
 * PKUNITY Power Manager (PM) Registers
 */
/*
 * PM Control Reg PM_PMCR
 */
#define PM_PMCR                 (PKUNITY_PM_BASE + 0x0000)
/*
 * PM General Conf. Reg PM_PGCR
 */
#define PM_PGCR                 (PKUNITY_PM_BASE + 0x0004)
/*
 * PM PLL Conf. Reg PM_PPCR
 */
#define PM_PPCR                 (PKUNITY_PM_BASE + 0x0008)
/*
 * PM Wakeup Enable Reg PM_PWER
 */
#define PM_PWER                 (PKUNITY_PM_BASE + 0x000C)
/*
 * PM GPIO Sleep Status Reg PM_PGSR
 */
#define PM_PGSR                 (PKUNITY_PM_BASE + 0x0010)
/*
 * PM Clock Gate Reg PM_PCGR
 */
#define PM_PCGR                 (PKUNITY_PM_BASE + 0x0014)
/*
 * PM SYS PLL Conf. Reg PM_PLLSYSCFG
 */
#define PM_PLLSYSCFG            (PKUNITY_PM_BASE + 0x0018)
/*
 * PM DDR PLL Conf. Reg PM_PLLDDRCFG
 */
#define PM_PLLDDRCFG            (PKUNITY_PM_BASE + 0x001C)
/*
 * PM VGA PLL Conf. Reg PM_PLLVGACFG
 */
#define PM_PLLVGACFG            (PKUNITY_PM_BASE + 0x0020)
/*
 * PM Div Conf. Reg PM_DIVCFG
 */
#define PM_DIVCFG               (PKUNITY_PM_BASE + 0x0024)
/*
 * PM SYS PLL Status Reg PM_PLLSYSSTATUS
 */
#define PM_PLLSYSSTATUS         (PKUNITY_PM_BASE + 0x0028)
/*
 * PM DDR PLL Status Reg PM_PLLDDRSTATUS
 */
#define PM_PLLDDRSTATUS         (PKUNITY_PM_BASE + 0x002C)
/*
 * PM VGA PLL Status Reg PM_PLLVGASTATUS
 */
#define PM_PLLVGASTATUS         (PKUNITY_PM_BASE + 0x0030)
/*
 * PM Div Status Reg PM_DIVSTATUS
 */
#define PM_DIVSTATUS            (PKUNITY_PM_BASE + 0x0034)
/*
 * PM Software Reset Reg PM_SWRESET
 */
#define PM_SWRESET              (PKUNITY_PM_BASE + 0x0038)
/*
 * PM DDR2 PAD Start Reg PM_DDR2START
 */
#define PM_DDR2START            (PKUNITY_PM_BASE + 0x003C)
/*
 * PM DDR2 PAD Status Reg PM_DDR2CAL0
 */
#define PM_DDR2CAL0             (PKUNITY_PM_BASE + 0x0040)
/*
 * PM PLL DFC Done Reg PM_PLLDFCDONE
 */
#define PM_PLLDFCDONE           (PKUNITY_PM_BASE + 0x0044)

#define PM_PMCR_SFB             FIELD(1, 1, 0)
#define PM_PMCR_IFB             FIELD(1, 1, 1)
#define PM_PMCR_CFBSYS          FIELD(1, 1, 2)
#define PM_PMCR_CFBDDR          FIELD(1, 1, 3)
#define PM_PMCR_CFBVGA          FIELD(1, 1, 4)
#define PM_PMCR_CFBDIVBCLK      FIELD(1, 1, 5)

/*
 * GPIO 8~27 wake-up enable PM_PWER_GPIOHIGH
 */
#define PM_PWER_GPIOHIGH        FIELD(1, 1, 8)
/*
 * RTC alarm wake-up enable PM_PWER_RTC
 */
#define PM_PWER_RTC             FIELD(1, 1, 31)

#define PM_PCGR_BCLK64DDR	FIELD(1, 1, 0)
#define PM_PCGR_BCLK64VGA	FIELD(1, 1, 1)
#define PM_PCGR_BCLKDDR		FIELD(1, 1, 2)
#define PM_PCGR_BCLKPCI		FIELD(1, 1, 4)
#define PM_PCGR_BCLKDMAC	FIELD(1, 1, 5)
#define PM_PCGR_BCLKUMAL	FIELD(1, 1, 6)
#define PM_PCGR_BCLKUSB		FIELD(1, 1, 7)
#define PM_PCGR_BCLKMME		FIELD(1, 1, 10)
#define PM_PCGR_BCLKNAND	FIELD(1, 1, 11)
#define PM_PCGR_BCLKH264E	FIELD(1, 1, 12)
#define PM_PCGR_BCLKVGA		FIELD(1, 1, 13)
#define PM_PCGR_BCLKH264D	FIELD(1, 1, 14)
#define PM_PCGR_VECLK		FIELD(1, 1, 15)
#define PM_PCGR_HECLK		FIELD(1, 1, 16)
#define PM_PCGR_HDCLK		FIELD(1, 1, 17)
#define PM_PCGR_NANDCLK		FIELD(1, 1, 18)
#define PM_PCGR_GECLK		FIELD(1, 1, 19)
#define PM_PCGR_VGACLK          FIELD(1, 1, 20)
#define PM_PCGR_PCICLK		FIELD(1, 1, 21)
#define PM_PCGR_SATACLK		FIELD(1, 1, 25)

/*
 * [23:20]PM_DIVCFG_VGACLK(v)
 */
#define PM_DIVCFG_VGACLK_MASK   FMASK(4, 20)
#define PM_DIVCFG_VGACLK(v)	FIELD((v), 4, 20)

#define PM_SWRESET_USB          FIELD(1, 1, 6)
#define PM_SWRESET_VGADIV       FIELD(1, 1, 26)
#define PM_SWRESET_GEDIV        FIELD(1, 1, 27)

#define PM_PLLDFCDONE_SYSDFC    FIELD(1, 1, 0)
#define PM_PLLDFCDONE_DDRDFC    FIELD(1, 1, 1)
#define PM_PLLDFCDONE_VGADFC    FIELD(1, 1, 2)
