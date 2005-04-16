#ifndef _LINUX_VFC_I2C_H_
#define _LINUX_VFC_I2C_H_

/* control bits */
#define PIN  (0x80000000)
#define ESO  (0x40000000)
#define ES1  (0x20000000)
#define ES2  (0x10000000)
#define ENI  (0x08000000)
#define STA  (0x04000000)
#define STO  (0x02000000)
#define ACK  (0x01000000)

/* status bits */
#define STS  (0x20000000)
#define BER  (0x10000000)
#define LRB  (0x08000000)
#define AAS  (0x04000000)
#define LAB  (0x02000000)
#define BB   (0x01000000)

#define SEND_I2C_START (PIN | ESO | STA)
#define SEND_I2C_STOP (PIN | ESO | STO)
#define CLEAR_I2C_BUS (PIN | ESO | ACK)
#define NEGATIVE_ACK ((ESO) & ~ACK)

#define SELECT(a) (a)
#define S0 (PIN | ESO | ES1)
#define S0_OWN (PIN)
#define S2 (PIN | ES1)
#define S3 (PIN | ES2)

#define ENABLE_SERIAL (PIN | ESO)
#define DISABLE_SERIAL (PIN)
#define RESET (PIN)

#define XMIT_LAST_BYTE (1)
#define VFC_I2C_ACK_CHECK (1)
#define VFC_I2C_NO_ACK_CHECK (0)

#endif /* _LINUX_VFC_I2C_H_ */



