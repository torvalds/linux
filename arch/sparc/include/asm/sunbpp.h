/*
 * include/asm/sunbpp.h
 */

#ifndef _ASM_SPARC_SUNBPP_H
#define _ASM_SPARC_SUNBPP_H

struct bpp_regs {
  /* DMA registers */
  __volatile__ __u32 p_csr;		/* DMA Control/Status Register */
  __volatile__ __u32 p_addr;		/* Address Register */
  __volatile__ __u32 p_bcnt;		/* Byte Count Register */
  __volatile__ __u32 p_tst_csr;		/* Test Control/Status (DMA2 only) */
  /* Parallel Port registers */
  __volatile__ __u16 p_hcr;		/* Hardware Configuration Register */
  __volatile__ __u16 p_ocr;		/* Operation Configuration Register */
  __volatile__ __u8 p_dr;		/* Parallel Data Register */
  __volatile__ __u8 p_tcr;		/* Transfer Control Register */
  __volatile__ __u8 p_or;		/* Output Register */
  __volatile__ __u8 p_ir;		/* Input Register */
  __volatile__ __u16 p_icr;		/* Interrupt Control Register */
};

/* P_HCR. Time is in increments of SBus clock. */
#define P_HCR_TEST      0x8000      /* Allows buried counters to be read */
#define P_HCR_DSW       0x7f00      /* Data strobe width (in ticks) */
#define P_HCR_DDS       0x007f      /* Data setup before strobe (in ticks) */

/* P_OCR. */
#define P_OCR_MEM_CLR   0x8000
#define P_OCR_DATA_SRC  0x4000      /* )                  */
#define P_OCR_DS_DSEL   0x2000      /* )  Bidirectional      */
#define P_OCR_BUSY_DSEL 0x1000      /* )    selects            */
#define P_OCR_ACK_DSEL  0x0800      /* )                  */
#define P_OCR_EN_DIAG   0x0400
#define P_OCR_BUSY_OP   0x0200      /* Busy operation */
#define P_OCR_ACK_OP    0x0100      /* Ack operation */
#define P_OCR_SRST      0x0080      /* Reset state machines. Not selfcleaning. */
#define P_OCR_IDLE      0x0008      /* PP data transfer state machine is idle */
#define P_OCR_V_ILCK    0x0002      /* Versatec faded. Zebra only. */
#define P_OCR_EN_VER    0x0001      /* Enable Versatec (0 - enable). Zebra only. */

/* P_TCR */
#define P_TCR_DIR       0x08
#define P_TCR_BUSY      0x04
#define P_TCR_ACK       0x02
#define P_TCR_DS        0x01        /* Strobe */

/* P_OR */
#define P_OR_V3         0x20        /* )                 */
#define P_OR_V2         0x10        /* ) on Zebra only   */
#define P_OR_V1         0x08        /* )                 */
#define P_OR_INIT       0x04
#define P_OR_AFXN       0x02        /* Auto Feed */
#define P_OR_SLCT_IN    0x01

/* P_IR */
#define P_IR_PE         0x04
#define P_IR_SLCT       0x02
#define P_IR_ERR        0x01

/* P_ICR */
#define P_DS_IRQ        0x8000      /* RW1  */
#define P_ACK_IRQ       0x4000      /* RW1  */
#define P_BUSY_IRQ      0x2000      /* RW1  */
#define P_PE_IRQ        0x1000      /* RW1  */
#define P_SLCT_IRQ      0x0800      /* RW1  */
#define P_ERR_IRQ       0x0400      /* RW1  */
#define P_DS_IRQ_EN     0x0200      /* RW   Always on rising edge */
#define P_ACK_IRQ_EN    0x0100      /* RW   Always on rising edge */
#define P_BUSY_IRP      0x0080      /* RW   1= rising edge */
#define P_BUSY_IRQ_EN   0x0040      /* RW   */
#define P_PE_IRP        0x0020      /* RW   1= rising edge */
#define P_PE_IRQ_EN     0x0010      /* RW   */
#define P_SLCT_IRP      0x0008      /* RW   1= rising edge */
#define P_SLCT_IRQ_EN   0x0004      /* RW   */
#define P_ERR_IRP       0x0002      /* RW1  1= rising edge */
#define P_ERR_IRQ_EN    0x0001      /* RW   */

#endif /* !(_ASM_SPARC_SUNBPP_H) */
