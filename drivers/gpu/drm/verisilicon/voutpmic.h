//define power management i2c
//#define PMIC_JH7110_51MCU_I2C_SLAVE_ADDRESS                       (0x50)
#define PMIC_JH7110_51MCU_I2C_SLAVE_REG_BASE                       0x80
#define PMIC_JH7110_51MCU_I2C_SLAVE_REG_END                        0x90
//#define PMIC_JH7110_51MCU_I2C_SLAVE_REG_NUM                        (PMIC_JH7110_51MCU_I2C_SLAVE_REG_END-PMIC_JH7110_51MCU_I2C_SLAVE_REG_BASE+1)

// #define PMIC_JH7110_51MCU_I2C_SLAVE_REG_TO_REG(x)                  ((x) > (PMIC_JH7110_51MCU_I2C_SLAVE_REG_END) ? \
//     (PMIC_JH7110_51MCU_I2C_SLAVE_REG_END-PMIC_JH7110_51MCU_I2C_SLAVE_REG_BASE) : (((x) < PMIC_JH7110_51MCU_I2C_SLAVE_REG_BASE) ? 0 : (x-PMIC_JH7110_51MCU_I2C_SLAVE_REG_BASE)))


//define power management i2c cmd(reg+data) 
#define POWER_SW_0_REG                           (0x00+PMIC_JH7110_51MCU_I2C_SLAVE_REG_BASE)
#define POWER_SW_0_VDD18_HDMI                    0
#define POWER_SW_0_VDD18_MIPITX                  1
#define POWER_SW_0_VDD18_MIPIRX                  2
#define POWER_SW_0_VDD09_HDMI                    3
#define POWER_SW_0_VDD09_MIPITX                  4
#define POWER_SW_0_VDD09_MIPIRX                  5

#define POWER_SW_1_REG                           (0x01+PMIC_JH7110_51MCU_I2C_SLAVE_REG_BASE)
#define POWER_SW_1_VDD1833_SD0_18                0



#define BIT(x)              (1UL<<(x))
typedef enum {
    PMU_DOMAIN_SYSTOP                   = BIT(0),
    PMU_DOMAIN_CPU                      = BIT(1),
    PMU_DOMAIN_GPUA                     = BIT(2),
    PMU_DOMAIN_VDEC                     = BIT(3),
    PMU_DOMAIN_VOUT                     = BIT(4),
    PMU_DOMAIN_ISP                      = BIT(5),
    PMU_DOMAIN_VENC                     = BIT(6),
    PMU_DOMAIN_ALL                      = (PMU_DOMAIN_SYSTOP|PMU_DOMAIN_CPU|PMU_DOMAIN_GPUA|PMU_DOMAIN_VDEC \
                                              |PMU_DOMAIN_VOUT|PMU_DOMAIN_ISP|PMU_DOMAIN_VENC),
    PMU_DOMAIN_PMIC_VDD18_HDMI          = BIT(16),
    PMU_DOMAIN_PMIC_VDD18_MIPITX        = BIT(17),
    PMU_DOMAIN_PMIC_VDD18_MIPIRX        = BIT(18),
    PMU_DOMAIN_PMIC_VDD09_HDMI          = BIT(19),
    PMU_DOMAIN_PMIC_VDD09_MIPITX        = BIT(20),
    PMU_DOMAIN_PMIC_VDD09_MIPIRX        = BIT(21),
    PMU_DOMAIN_PMIC_VDD1833_SD0_18      = BIT(22),
    PMU_DOMAIN_PMIC_ALL                 = (PMU_DOMAIN_PMIC_VDD18_HDMI|PMU_DOMAIN_PMIC_VDD18_MIPITX|PMU_DOMAIN_PMIC_VDD18_MIPIRX \
                                            |PMU_DOMAIN_PMIC_VDD09_HDMI|PMU_DOMAIN_PMIC_VDD09_MIPITX|PMU_DOMAIN_PMIC_VDD09_MIPIRX \
                                            |PMU_DOMAIN_PMIC_VDD1833_SD0_18),
} sys_pmu_domain_t;

enum pmic_domain {
    PMIC_DOMAIN_0       = 0,
    PMIC_DOMAIN_1       = 1,
    PMIC_DOMAIN_2       = 2,
    PMIC_DOMAIN_3       = 3,
    PMIC_DOMAIN_4       = 4,
    PMIC_DOMAIN_5       = 5,
    PMIC_DOMAIN_6       = 6,
    PMIC_DOMAIN_7       = 7,
    PMIC_DOMAIN_8       = 8,
    PMIC_DOMAIN_9       = 9,
    PMIC_DOMAIN_10      = 10,
    PMIC_DOMAIN_11      = 11,
    PMIC_DOMAIN_12      = 12,
    PMIC_DOMAIN_13      = 13,
    PMIC_DOMAIN_14      = 14,
    PMIC_DOMAIN_15      = 15,
};

static struct {
    int pmu_dom;
    int pmic_dom;
} pmu_pmic_table[] = {
    { PMU_DOMAIN_PMIC_VDD18_HDMI,        PMIC_DOMAIN_0 },
    { PMU_DOMAIN_PMIC_VDD18_MIPITX,      PMIC_DOMAIN_1 },
    { PMU_DOMAIN_PMIC_VDD18_MIPIRX,      PMIC_DOMAIN_2 },
    { PMU_DOMAIN_PMIC_VDD09_HDMI,        PMIC_DOMAIN_3 },
    { PMU_DOMAIN_PMIC_VDD09_MIPITX,      PMIC_DOMAIN_4 },
    { PMU_DOMAIN_PMIC_VDD09_MIPIRX,      PMIC_DOMAIN_5 },
    { PMU_DOMAIN_PMIC_VDD1833_SD0_18,    PMIC_DOMAIN_6 },
};

enum pmic_switch {
    PMIC_SWITCH_OFF,
    PMIC_SWITCH_ON,
};