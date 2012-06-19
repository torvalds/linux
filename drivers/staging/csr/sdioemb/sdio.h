/*
 * Standard SDIO definitions.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef SDIOEMB_SDIO_H
#define SDIOEMB_SDIO_H

/* Maximum time for VDD to rise to VDD min. */
#define SDIO_POWER_UP_TIME_MS 250

/* Minimum SD bus clock a card must support (Hz). */
#define SDIO_CLOCK_FREQ_MIN 400000

/* Maximum clock frequency for normal mode (Hz).
 *
 * Although high speed mode should be suitable for all speeds not all
 * controller/card combinations are capable of meeting the higher
 * tolerances for (e.g.) clock rise/fall times.  Therefore, default
 * mode is used where possible for improved compatibility. */
#define SDIO_CLOCK_FREQ_NORMAL_SPD 25000000

/* Maximum clock frequency for high speed mode (Hz). */
#define SDIO_CLOCK_FREQ_HIGH_SPD   50000000

#define SDIO_MAX_FUNCTIONS 8 /* incl. F0 */

/* Command argument format. */

#define SDIO_CMD52_ARG_WRITE   0x80000000
#define SDIO_CMD52_ARG_FUNC(f) ((f) << 28)
#define SDIO_CMD52_ARG_ADDR(a) ((a) << 9)
#define SDIO_CMD52_ARG_DATA(d) ((d) << 0)

#define SDIO_CMD53_ARG_WRITE    0x80000000
#define SDIO_CMD53_ARG_FUNC(f)  ((f) << 28)
#define SDIO_CMD53_ARG_BLK_MODE 0x08000000
#define SDIO_CMD53_ARG_ADDR(a)  ((a) << 9)
#define SDIO_CMD53_ARG_CNT(c)   ((c) << 0)

/* Response format. */

#define SDIO_R5_DATA(r) (((r) >> 0) & 0xff)
#define SDIO_R5_OUT_OF_RANGE    (1 <<  8)
#define SDIO_R5_FUNCTION_NUMBER (1 <<  9)
#define SDIO_R5_ERROR           (1 << 11)

/* Register offsets and bits. */

#define SDIO_OCR_CARD_READY     0x80000000
#define SDIO_OCR_NUM_FUNCS_MASK 0x70000000
#define SDIO_OCR_NUM_FUNCS_OFFSET 28
#define SDIO_OCR_VOLTAGE_3V3    0x00300000 /* 3.2-3.3V & 3.3-3.4V */

#define SDIO_CCCR_SDIO_REV       0x00
#define SDIO_CCCR_SD_REV         0x01
#define SDIO_CCCR_IO_EN          0x02
#define SDIO_CCCR_IO_READY       0x03
#define SDIO_CCCR_INT_EN         0x04
#  define SDIO_CCCR_INT_EN_MIE   0x01
#define SDIO_CCCR_INT_PENDING    0x05
#define SDIO_CCCR_IO_ABORT       0x06
#define SDIO_CCCR_BUS_IFACE_CNTL 0x07
#  define SDIO_CCCR_BUS_IFACE_CNTL_CD_R_DISABLE 0x80
#  define SDIO_CCCR_BUS_IFACE_CNTL_ECSI         0x20
#  define SDIO_CCCR_BUS_IFACE_CNTL_4BIT_BUS     0x02
#define SDIO_CCCR_CARD_CAPS      0x08
#  define SDIO_CCCR_CARD_CAPS_LSC  0x40
#  define SDIO_CCCR_CARD_CAPS_4BLS 0x80
#define SDIO_CCCR_CIS_PTR        0x09
#define SDIO_CCCR_BUS_SUSPEND    0x0c
#define SDIO_CCCR_FUNC_SEL       0x0d
#define SDIO_CCCR_EXEC_FLAGS     0x0e
#define SDIO_CCCR_READY_FLAGS    0x0f
#define SDIO_CCCR_F0_BLK_SIZE    0x10
#define SDIO_CCCR_PWR_CNTL       0x12
#define SDIO_CCCR_HIGH_SPEED     0x13
#  define SDIO_CCCR_HIGH_SPEED_SHS 0x01
#  define SDIO_CCCR_HIGH_SPEED_EHS 0x02

#define SDIO_FBR_REG(f, r) (0x100*(f) + (r))

#define SDIO_FBR_STD_IFACE(f)     SDIO_FBR_REG(f, 0x00)
#define SDIO_FBR_STD_IFACE_EXT(f) SDIO_FBR_REG(f, 0x01)
#define SDIO_FBR_CIS_PTR(f)       SDIO_FBR_REG(f, 0x09)
#define SDIO_FBR_CSA_PTR(f)       SDIO_FBR_REG(f, 0x0c)
#define SDIO_FBR_CSA_DATA(f)      SDIO_FBR_REG(f, 0x0f)
#define SDIO_FBR_BLK_SIZE(f)      SDIO_FBR_REG(f, 0x10)

#define SDIO_STD_IFACE_UART      0x01
#define SDIO_STD_IFACE_BT_TYPE_A 0x02
#define SDIO_STD_IFACE_BT_TYPE_B 0x03
#define SDIO_STD_IFACE_GPS       0x04
#define SDIO_STD_IFACE_CAMERA    0x05
#define SDIO_STD_IFACE_PHS       0x06
#define SDIO_STD_IFACE_WLAN      0x07
#define SDIO_STD_IFACE_BT_TYPE_A_AMP 0x09

/*
 * Manufacturer and card IDs.
 */
#define SDIO_MANF_ID_CSR        0x032a

#define SDIO_CARD_ID_CSR_UNIFI_1        0x0001
#define SDIO_CARD_ID_CSR_UNIFI_2        0x0002
#define SDIO_CARD_ID_CSR_BC6            0x0004
#define SDIO_CARD_ID_CSR_DASH_D00       0x0005
#define SDIO_CARD_ID_CSR_BC7            0x0006
#define SDIO_CARD_ID_CSR_CINDERELLA     0x0007
#define SDIO_CARD_ID_CSR_UNIFI_3        0x0007
#define SDIO_CARD_ID_CSR_UNIFI_4        0x0008
#define SDIO_CARD_ID_CSR_DASH           0x0010

#endif /* #ifndef SDIOEMB_SDIO_H */
