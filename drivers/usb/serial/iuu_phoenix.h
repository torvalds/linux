// SPDX-License-Identifier: GPL-2.0+
/*
 * Infinity Unlimited USB Phoenix driver
 *
 * Copyright (C) 2007 Alain Degreffe (eczema@ecze.com)
 *
 *
 * Original code taken from iuutool ( Copyright (C) 2006 Juan Carlos Borrás )
 *
 *  And tested with help of WB Electronics
 */

#define   IUU_USB_VENDOR_ID  0x104f
#define   IUU_USB_PRODUCT_ID  0x0004
#define   IUU_USB_OP_TIMEOUT  0x0200

/* Programmer commands */

#define IUU_NO_OPERATION   0x00
#define IUU_GET_FIRMWARE_VERSION   0x01
#define IUU_GET_PRODUCT_NAME   0x02
#define IUU_GET_STATE_REGISTER   0x03
#define IUU_SET_LED   0x04
#define IUU_WAIT_MUS   0x05
#define IUU_WAIT_MS   0x06
#define IUU_GET_LOADER_VERSION   0x50
#define IUU_RST_SET   0x52
#define IUU_RST_CLEAR   0x53
#define IUU_SET_VCC   0x59
#define IUU_UART_ENABLE   0x49
#define IUU_UART_DISABLE   0x4A
#define IUU_UART_WRITE_I2C   0x4C
#define IUU_UART_ESC   0x5E
#define IUU_UART_TRAP   0x54
#define IUU_UART_TRAP_BREAK   0x5B
#define IUU_UART_RX   0x56
#define IUU_AVR_ON   0x21
#define IUU_AVR_OFF   0x22
#define IUU_AVR_1CLK   0x23
#define IUU_AVR_RESET   0x24
#define IUU_AVR_RESET_PC   0x25
#define IUU_AVR_INC_PC   0x26
#define IUU_AVR_INCN_PC   0x27
#define IUU_AVR_PREAD   0x29
#define IUU_AVR_PREADN   0x2A
#define IUU_AVR_PWRITE   0x28
#define IUU_AVR_DREAD   0x2C
#define IUU_AVR_DREADN   0x2D
#define IUU_AVR_DWRITE   0x2B
#define IUU_AVR_PWRITEN   0x2E
#define IUU_EEPROM_ON   0x37
#define IUU_EEPROM_OFF   0x38
#define IUU_EEPROM_WRITE   0x39
#define IUU_EEPROM_WRITEX   0x3A
#define IUU_EEPROM_WRITE8   0x3B
#define IUU_EEPROM_WRITE16   0x3C
#define IUU_EEPROM_WRITEX32   0x3D
#define IUU_EEPROM_WRITEX64   0x3E
#define IUU_EEPROM_READ   0x3F
#define IUU_EEPROM_READX   0x40
#define IUU_EEPROM_BREAD   0x41
#define IUU_EEPROM_BREADX   0x42
#define IUU_PIC_CMD   0x0A
#define IUU_PIC_CMD_LOAD   0x0B
#define IUU_PIC_CMD_READ   0x0C
#define IUU_PIC_ON   0x0D
#define IUU_PIC_OFF   0x0E
#define IUU_PIC_RESET   0x16
#define IUU_PIC_INC_PC   0x0F
#define IUU_PIC_INCN_PC   0x10
#define IUU_PIC_PWRITE   0x11
#define IUU_PIC_PREAD   0x12
#define IUU_PIC_PREADN   0x13
#define IUU_PIC_DWRITE   0x14
#define IUU_PIC_DREAD   0x15
#define IUU_UART_NOP   0x00
#define IUU_UART_CHANGE   0x02
#define IUU_UART_TX   0x04
#define IUU_DELAY_MS   0x06

#define IUU_OPERATION_OK   0x00
#define IUU_DEVICE_NOT_FOUND   0x01
#define IUU_INVALID_HANDLE   0x02
#define IUU_INVALID_PARAMETER   0x03
#define IUU_INVALID_voidERFACE   0x04
#define IUU_INVALID_REQUEST_LENGTH   0x05
#define IUU_UART_NOT_ENABLED   0x06
#define IUU_WRITE_ERROR   0x07
#define IUU_READ_ERROR   0x08
#define IUU_TX_ERROR   0x09
#define IUU_RX_ERROR   0x0A

#define IUU_PARITY_NONE   0x00
#define IUU_PARITY_EVEN   0x01
#define IUU_PARITY_ODD   0x02
#define IUU_PARITY_MARK   0x03
#define IUU_PARITY_SPACE   0x04
#define IUU_SC_INSERTED   0x01
#define IUU_VERIFY_ERROR   0x02
#define IUU_SIM_INSERTED   0x04
#define IUU_TWO_STOP_BITS   0x00
#define IUU_ONE_STOP_BIT   0x20
#define IUU_BAUD_2400   0x0398
#define IUU_BAUD_9600   0x0298
#define IUU_BAUD_19200   0x0164
#define IUU_BAUD_28800   0x0198
#define IUU_BAUD_38400   0x01B2
#define IUU_BAUD_57600   0x0030
#define IUU_BAUD_115200   0x0098
#define IUU_CLK_3579000   3579000
#define IUU_CLK_3680000   3680000
#define IUU_CLK_6000000   6000000
#define IUU_FULLCARD_IN   0x01
#define IUU_DEV_ERROR   0x02
#define IUU_MINICARD_IN   0x04
#define IUU_VCC_5V   0x00
#define IUU_VCC_3V   0x01
