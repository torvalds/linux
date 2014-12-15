#ifndef  ALWAYS_ON_REGS_H
#define ALWAYS_ON_REGS_H
// -------------------------------------------------------------------
// BASE #0
// -------------------------------------------------------------------

#define AO_RTI_STATUS_REG0        (((0x00 << 8) | (0x00))<<2)
#define AO_RTI_STATUS_REG1        (((0x00 << 8) | (0x01))<<2)
#define AO_RTI_STATUS_REG2        (((0x00 << 8) | (0x02))<<2)
#define AO_RTI_PWR_CNTL_REG0      (((0x00 << 8) | (0x04))<<2)
#define AO_RTI_PIN_MUX_REG        (((0x00 << 8) | (0x05))<<2)
#define AO_WD_GPIO_REG            (((0x00 << 8) | (0x06))<<2)
#define AO_REMAP_REG0             (((0x00 << 8) | (0x07))<<2)
#define AO_REMAP_REG1             (((0x00 << 8) | (0x08))<<2)
#define AO_GPIO_O_EN_N            (((0x00 << 8) | (0x09))<<2)
#define AO_GPIO_I                 (((0x00 << 8) | (0x0A))<<2)
#define AO_RTI_PULL_UP_REG        (((0x00 << 8) | (0x0B))<<2)
#define AO_RTI_JTAG_CODNFIG_REG   (((0x00 << 8) | (0x0C))<<2)
#define AO_RTI_WD_MARK            (((0x00 << 8) | (0x0D))<<2)
#define AO_RTI_GEN_CNTL_REG0      (((0x00 << 8) | (0x10))<<2)
#define AO_WATCHDOG_REG           (((0x00 << 8) | (0x11))<<2)
#define AO_WATCHDOG_RESET         (((0x00 << 8) | (0x12))<<2)
#define AO_TIMER_REG              (((0x00 << 8) | (0x13))<<2)
#define AO_TIMERA_REG             (((0x00 << 8) | (0x14))<<2)
#define AO_TIMERE_REG             (((0x00 << 8) | (0x15))<<2)
#define AO_AHB2DDR_CNTL           (((0x00 << 8) | (0x18))<<2)
#define AO_IRQ_MASK_FIQ_SEL       (((0x00 << 8) | (0x20))<<2)
#define AO_IRQ_GPIO_REG           (((0x00 << 8) | (0x21))<<2)
#define AO_IRQ_STAT               (((0x00 << 8) | (0x22))<<2)
#define AO_IRQ_STAT_CLR           (((0x00 << 8) | (0x23))<<2)
#define AO_DEBUG_REG0             (((0x00 << 8) | (0x28))<<2)
#define AO_DEBUG_REG1             (((0x00 << 8) | (0x29))<<2)
#define AO_DEBUG_REG2             (((0x00 << 8) | (0x2a))<<2)
#define AO_DEBUG_REG3             (((0x00 << 8) | (0x2b))<<2)
// -------------------------------------------------------------------
// BASE #1
// -------------------------------------------------------------------
#define AO_IR_DEC_LDR_ACTIVE      (((0x01 << 8) | (0x20))<<2)
#define AO_IR_DEC_LDR_IDLE        (((0x01 << 8) | (0x21))<<2)
#define AO_IR_DEC_LDR_REPEAT      (((0x01 << 8) | (0x22))<<2)
#define AO_IR_DEC_BIT_0           (((0x01 << 8) | (0x23))<<2)
#define AO_IR_DEC_REG0            (((0x01 << 8) | (0x24))<<2)
#define AO_IR_DEC_FRAME           (((0x01 << 8) | (0x25))<<2)
#define AO_IR_DEC_STATUS          (((0x01 << 8) | (0x26))<<2)
#define AO_IR_DEC_REG1            (((0x01 << 8) | (0x27))<<2)
// ----------------------------
// UART
// ----------------------------
#define AO_UART_WFIFO             (((0x01 << 8) | (0x30))<<2)
#define AO_UART_RFIFO             (((0x01 << 8) | (0x31))<<2)
#define AO_UART_CONTROL           (((0x01 << 8) | (0x32))<<2)
#define AO_UART_STATUS            (((0x01 << 8) | (0x33))<<2)
#define AO_UART_MISC              (((0x01 << 8) | (0x34))<<2)
// ----------------------------
// I2C Master (8)
// ----------------------------
#define AO_I2C_M_0_CONTROL_REG    (((0x01 << 8) | (0x40))<<2)
#define AO_I2C_M_0_SLAVE_ADDR     (((0x01 << 8) | (0x41))<<2)
#define AO_I2C_M_0_TOKEN_LIST0    (((0x01 << 8) | (0x42))<<2)
#define AO_I2C_M_0_TOKEN_LIST1    (((0x01 << 8) | (0x43))<<2)
#define AO_I2C_M_0_WDATA_REG0     (((0x01 << 8) | (0x44))<<2)
#define AO_I2C_M_0_WDATA_REG1     (((0x01 << 8) | (0x45))<<2)
#define AO_I2C_M_0_RDATA_REG0     (((0x01 << 8) | (0x46))<<2)
#define AO_I2C_M_0_RDATA_REG1     (((0x01 << 8) | (0x47))<<2)
// ----------------------------
// I2C Slave (3)
// ----------------------------
#define AO_I2C_S_CONTROL_REG      (((0x01 << 8) | (0x50))<<2)
#define AO_I2C_S_SEND_REG         (((0x01 << 8) | (0x51))<<2)
#define AO_I2C_S_RECV_REG         (((0x01 << 8) | (0x52))<<2)
#define AO_I2C_S_CNTL1_REG        (((0x01 << 8) | (0x53))<<2)
// ---------------------------
// RTC (4)
// ---------------------------
//#define AO_RTC_ADDR0              (((0x01 << 8) | (0xd0))<<2)
//#define AO_RTC_ADDR1              (((0x01 << 8) | (0xd1))<<2)
//#define AO_RTC_ADDR2              (((0x01 << 8) | (0xd2))<<2)
//#define AO_RTC_ADDR3              (((0x01 << 8) | (0xd3))<<2)
//#define AO_RTC_ADDR4              (((0x01 << 8) | (0xd4))<<2)

#endif      //ALWAYS_ON_REGS_H



