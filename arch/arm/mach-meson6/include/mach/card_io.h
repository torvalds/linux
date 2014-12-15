#ifndef __CARD_IO_H
#define __CARD_IO_H

#include <mach/am_regs.h>
#include <linux/types.h>

/**
 * @file card_io.h
 * @addtogroup Card
 */
/*@{*/

/** Card module */
typedef enum _Card_Module {
    CARD_MODULE_CF,
    CARD_MODULE_SD_MMC,
    CARD_MODULE_INAND,
    CARD_MODULE_MS_MSPRO,
    CARD_MODULE_MS2,
    CARD_MODULE_XD,
    CARD_MODULE_SM
} Card_Module_t;

#define MAX_CARD_UNIT       (CARD_MODULE_SM+1)
/** Card config */
typedef struct _Card_Config {
    int cf_enabled;
    int sd_mmc_enabled;
    int inand_enabled;
    int ms_mspro_enabled;
    int ms2_enabled;
    int xd_enabled;
    int sm_enabled;
    int sd_wifi_enable;
} Card_Config_t;

typedef enum _SDIO_Pad_Type {
	
	SDHC_CARD_0_5,	//SDHC-B
	SDHC_BOOT_0_11,	//SDHC-C
	SDHC_GPIOX_0_9,	//SDHC-A

	SDXC_CARD_0_5,	//SDXC-B
	SDXC_BOOT_0_11,	//SDXC-C
	SDXC_GPIOX_0_9	//SDXC-A
} SDIO_Pad_Type_t;

typedef enum _Card_Work_Mode {
    CARD_HW_MODE,
    CARD_SW_MODE
} Card_Work_Mode_t;

struct aml_card_info {
    char *name;         /* card name  */
    Card_Work_Mode_t work_mode; /* work mode select*/
    SDIO_Pad_Type_t  io_pad_type;   /* hw io pin pad */
    unsigned card_ins_en_reg;
    unsigned card_ins_en_mask;
    unsigned card_ins_input_reg;
    unsigned card_ins_input_mask;
    unsigned card_power_en_reg;
    unsigned card_power_en_mask;
    unsigned card_power_output_reg;
    unsigned card_power_output_mask;
    unsigned char card_power_en_lev;
    unsigned card_wp_en_reg;
    unsigned card_wp_en_mask;
    unsigned card_wp_input_reg;
    unsigned card_wp_input_mask;
    void (*card_extern_init)(void);
    /*for inand partition: struct mtd_partition, easy porting from nand*/
    struct mtd_partition    *partitions;
    unsigned int           nr_partitions;
};

struct aml_card_platform {
    u8 card_num;
    struct aml_card_info *card_info;
};

struct card_partition {
    char *name;         /* identifier string */
    uint64_t size;          /* partition size */
    uint64_t offset;        /* offset within the memory card space */
    uint32_t mask_flags;        /* master card flags to mask out for this partition */
};

#define CARD_PIN_MUX_0                  PERIPHS_PIN_MUX_0

/// Muxing contorl
#define CARD_PIN_MUX_1                  PERIPHS_PIN_MUX_1

/// Muxing contorl
#define CARD_PIN_MUX_2                  PERIPHS_PIN_MUX_2

/// Muxing contorl
#define CARD_PIN_MUX_3                  PERIPHS_PIN_MUX_3

/// Muxing contorl
#define CARD_PIN_MUX_4                  PERIPHS_PIN_MUX_4
#define CARD_PIN_MUX_5                  PERIPHS_PIN_MUX_5
#define CARD_PIN_MUX_6                  PERIPHS_PIN_MUX_6
#define CARD_PIN_MUX_7                  PERIPHS_PIN_MUX_7
#define CARD_PIN_MUX_8                  PERIPHS_PIN_MUX_8
#define CARD_PIN_MUX_9                  PERIPHS_PIN_MUX_9
#define CARD_PIN_MUX_10                 PERIPHS_PIN_MUX_10
#define CARD_PIN_MUX_11                 PERIPHS_PIN_MUX_11
#define CARD_PIN_MUX_12                 PERIPHS_PIN_MUX_12

#define CARD_GPIO_ENABLE            CBUS_REG_ADDR(PREG_PAD_GPIO5_EN_N)
#define CARD_GPIO_OUTPUT            CBUS_REG_ADDR(PREG_PAD_GPIO5_O)
#define CARD_GPIO_INPUT             CBUS_REG_ADDR(PREG_PAD_GPIO5_I)

#define BOOT_GPIO_ENABLE            CBUS_REG_ADDR(PREG_PAD_GPIO3_EN_N)
#define BOOT_GPIO_OUTPUT            CBUS_REG_ADDR(PREG_PAD_GPIO3_O)
#define BOOT_GPIO_INPUT             CBUS_REG_ADDR(PREG_PAD_GPIO3_I)

#define EGPIO_GPIOA_ENABLE			CBUS_REG_ADDR(PREG_PAD_GPIO0_EN_N)
#define EGPIO_GPIOA_OUTPUT			CBUS_REG_ADDR(PREG_PAD_GPIO0_O)
#define EGPIO_GPIOA_INPUT			CBUS_REG_ADDR(PREG_PAD_GPIO0_I)

#define EGPIO_GPIOB_ENABLE			CBUS_REG_ADDR(PREG_PAD_GPIO1_EN_N)
#define EGPIO_GPIOB_OUTPUT			CBUS_REG_ADDR(PREG_PAD_GPIO1_O)
#define EGPIO_GPIOB_INPUT			CBUS_REG_ADDR(PREG_PAD_GPIO1_I)

#define EGPIO_GPIOC_ENABLE			CBUS_REG_ADDR(PREG_PAD_GPIO2_EN_N)
#define EGPIO_GPIOC_OUTPUT			CBUS_REG_ADDR(PREG_PAD_GPIO2_O)
#define EGPIO_GPIOC_INPUT			CBUS_REG_ADDR(PREG_PAD_GPIO2_I)

#define EGPIO_GPIOD_ENABLE			CBUS_REG_ADDR(PREG_PAD_GPIO2_EN_N)
#define EGPIO_GPIOD_OUTPUT			CBUS_REG_ADDR(PREG_PAD_GPIO2_O)
#define EGPIO_GPIOD_INPUT			CBUS_REG_ADDR(PREG_PAD_GPIO2_I)

#define EGPIO_GPIOE_ENABLE			CBUS_REG_ADDR(PREG_PAD_GPIO6_EN_N)
#define EGPIO_GPIOE_OUTPUT			CBUS_REG_ADDR(PREG_PAD_GPIO6_O)
#define EGPIO_GPIOE_INPUT			CBUS_REG_ADDR(PREG_PAD_GPIO6_I)

#define EGPIO_GPIOXL_ENABLE			CBUS_REG_ADDR(PREG_PAD_GPIO4_EN_N)
#define EGPIO_GPIOXL_OUTPUT			CBUS_REG_ADDR(PREG_PAD_GPIO4_O)
#define EGPIO_GPIOXL_INPUT			CBUS_REG_ADDR(PREG_PAD_GPIO4_I)

#define EGPIO_GPIOXH_ENABLE			CBUS_REG_ADDR(PREG_PAD_GPIO3_EN_N)
#define EGPIO_GPIOXH_OUTPUT			CBUS_REG_ADDR(PREG_PAD_GPIO3_O)
#define EGPIO_GPIOXH_INPUT			CBUS_REG_ADDR(PREG_PAD_GPIO3_I)

#define EGPIO_GPIOY_ENABLE			CBUS_REG_ADDR(PREG_PAD_GPIO5_EN_N)
#define EGPIO_GPIOY_OUTPUT			CBUS_REG_ADDR(PREG_PAD_GPIO5_O)
#define EGPIO_GPIOY_INPUT			CBUS_REG_ADDR(PREG_PAD_GPIO5_I)

#define EGPIO_GPIOZ_ENABLE			CBUS_REG_ADDR(PREG_PAD_GPIO6_EN_N)
#define EGPIO_GPIOZ_OUTPUT			CBUS_REG_ADDR(PREG_PAD_GPIO6_O)
#define EGPIO_GPIOZ_INPUT			CBUS_REG_ADDR(PREG_PAD_GPIO6_I)

#define EGPIO_GPIOAO_ENABLE			AOBUS_REG_ADDR(AO_GPIO_O_EN_N)
#define EGPIO_GPIOAO_OUTPUT			AOBUS_REG_ADDR(AO_GPIO_O_EN_N)
#define EGPIO_GPIOAO_INPUT			AOBUS_REG_ADDR(AO_GPIO_I)

//JTAG group
#define JTAG_GPIO_ENABLE                CBUS_REG_ADDR(PREG_JTAG_GPIO_ADDR)
#define JTAG_GPIO_OUTPUT                CBUS_REG_ADDR(PREG_JTAG_GPIO_ADDR)
#define JTAG_GPIO_INPUT                 CBUS_REG_ADDR(PREG_JTAG_GPIO_ADDR)

#define TMS_MASK_ENABLE                 0x00000002L
#define TDI_MASK_ENABLE                 0x00000004L
#define TCK_MASK_ENABLE                 0x00000001L
#define TDO_MASK_ENABLE                 0x00000008L
#define TEST_N_MASK_ENABLE              0x00010000L
#define TMS_MASK_OUTPUT                 0x00000020L
#define TDI_MASK_OUTPUT                 0x00000040L
#define TCK_MASK_OUTPUT                 0x00000010L
#define TDO_MASK_OUTPUT                 0x00000080L
#define TEST_N_MASK_OUTPUT              0x00100000L
#define TMS_MASK_INPUT                  0x00000200L
#define TDI_MASK_INPUT                  0x00000400L
#define TCK_MASK_INPUT                  0x00000100L
#define TDO_MASK_INPUT                  0x00000800L


#define PREG_IO_0_MASK                  0x00000001L
#define PREG_IO_1_MASK                  0x00000002L
#define PREG_IO_2_MASK                  0x00000004L
#define PREG_IO_3_MASK                  0x00000008L
#define PREG_IO_4_MASK                  0x00000010L
#define PREG_IO_5_MASK                  0x00000020L
#define PREG_IO_6_MASK                  0x00000040L
#define PREG_IO_7_MASK                  0x00000080L
#define PREG_IO_8_MASK                  0x00000100L
#define PREG_IO_9_MASK                  0x00000200L
#define PREG_IO_10_MASK                 0x00000400L
#define PREG_IO_11_MASK                 0x00000800L
#define PREG_IO_12_MASK                 0x00001000L
#define PREG_IO_13_MASK                 0x00002000L
#define PREG_IO_14_MASK                 0x00004000L
#define PREG_IO_15_MASK                 0x00008000L
#define PREG_IO_16_MASK                 0x00010000L
#define PREG_IO_17_MASK                 0x00020000L
#define PREG_IO_18_MASK                 0x00040000L
#define PREG_IO_19_MASK                 0x00080000L
#define PREG_IO_20_MASK                 0x00100000L
#define PREG_IO_21_MASK                 0x00200000L
#define PREG_IO_22_MASK                 0x00400000L
#define PREG_IO_23_MASK                 0x00800000L
#define PREG_IO_24_MASK                 0x01000000L
#define PREG_IO_25_MASK                 0x02000000L
#define PREG_IO_26_MASK                 0x04000000L
#define PREG_IO_27_MASK                 0x08000000L
#define PREG_IO_28_MASK                 0x10000000L
#define PREG_IO_29_MASK                 0x20000000L
#define PREG_IO_30_MASK                 0x40000000L
#define PREG_IO_31_MASK                 0x80000000L

#define PREG_IO_0_3_MASK                0x0000000FL
#define PREG_IO_2_5_MASK                0x0000003CL
#define PREG_IO_4_7_MASK                0x000000F0L
#define PREG_IO_0_7_MASK                0x000000FFL
#define PREG_IO_8_11_MASK               0x00000F00L
#define PREG_IO_8_15_MASK               0x0000FF00L
#define PREG_IO_9_16_MASK               0x0001FE00L
#define PREG_IO_10_13_MASK              0x00003c00L
#define PREG_IO_12_15_MASK              0x0000F000L
#define PREG_IO_13_16_MASK              0x0001E000L
#define PREG_IO_14_17_MASK              0x0003C000L
#define PREG_IO_17_20_MASK              0x001E0000L
#define PREG_IO_22_25_MASK              0x03C00000L
#define PREG_IO_23_26_MASK              0x07800000L
#define PREG_IO_24_27_MASK              0x0F000000L
#define PREG_IO_22_29_MASK              0x3FC00000L

#define CARD_HW_MODE                    0
#define CARD_SW_MODE                    1
#define XD_NAND_MODE                    2

#define CARD_SLOT_4_1                   0
#define CARD_SLOT_DISJUNCT              1

#define ATA_DEV_SLEEP                   0
#define ATA_DEV_RECOVER                 1

#define SDIO_NO_INT                   0
#define SDIO_IF_INT                   1
#define SDIO_CMD_INT                  2
#define SDIO_SOFT_INT                 3
#define SDIO_TIMEOUT_INT              4

#endif //__CARD_IO_H

