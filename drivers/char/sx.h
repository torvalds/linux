
/*
 *  sx.h
 *
 *  Copyright (C) 1998/1999 R.E.Wolff@BitWizard.nl
 *
 *  SX serial driver.
 *  -- Supports SI, XIO and SX host cards. 
 *  -- Supports TAs, MTAs and SXDCs.
 *
 *  Version 1.3 -- March, 1999. 
 * 
 */

#define SX_NBOARDS        4
#define SX_PORTSPERBOARD 32
#define SX_NPORTS        (SX_NBOARDS * SX_PORTSPERBOARD)

#ifdef __KERNEL__

#define SX_MAGIC 0x12345678

struct sx_port {
  struct gs_port          gs;
  struct wait_queue       *shutdown_wait;
  int                     ch_base;
  int                     c_dcd;
  struct sx_board         *board;
  int                     line;
  long                    locks;
};

struct sx_board {
  int magic;
  void __iomem *base;
  void __iomem *base2;
  unsigned long hw_base;
  resource_size_t hw_len;
  int eisa_base;
  int port_base; /* Number of the first port */
  struct sx_port *ports;
  int nports;
  int flags;
  int irq;
  int poll;
  int ta_type;
  struct timer_list       timer;
  long                    locks;
};

struct vpd_prom {
  unsigned short id;
  char hwrev;
  char hwass;
  int uniqid;
  char myear;
  char mweek;
  char hw_feature[5];
  char oem_id;
  char identifier[16];
};

#ifndef MOD_RS232DB25MALE
#define MOD_RS232DB25MALE 0x0a
#endif

#define SI_ISA_BOARD         0x00000001
#define SX_ISA_BOARD         0x00000002
#define SX_PCI_BOARD         0x00000004
#define SX_CFPCI_BOARD       0x00000008
#define SX_CFISA_BOARD       0x00000010
#define SI_EISA_BOARD        0x00000020
#define SI1_ISA_BOARD        0x00000040

#define SX_BOARD_PRESENT     0x00001000
#define SX_BOARD_INITIALIZED 0x00002000
#define SX_IRQ_ALLOCATED     0x00004000

#define SX_BOARD_TYPE        0x000000ff

#define IS_SX_BOARD(board) (board->flags & (SX_PCI_BOARD | SX_CFPCI_BOARD | \
                                            SX_ISA_BOARD | SX_CFISA_BOARD))

#define IS_SI_BOARD(board) (board->flags & SI_ISA_BOARD)
#define IS_SI1_BOARD(board) (board->flags & SI1_ISA_BOARD)

#define IS_EISA_BOARD(board) (board->flags & SI_EISA_BOARD)

#define IS_CF_BOARD(board) (board->flags & (SX_CFISA_BOARD | SX_CFPCI_BOARD))

#define SERIAL_TYPE_NORMAL 1

/* The SI processor clock is required to calculate the cc_int_count register
   value for the SI cards. */
#define SI_PROCESSOR_CLOCK 25000000


/* port flags */
/* Make sure these don't clash with gs flags or async flags */
#define SX_RX_THROTTLE        0x0000001



#define SX_PORT_TRANSMIT_LOCK  0
#define SX_BOARD_INTR_LOCK     0



/* Debug flags. Add these together to get more debug info. */

#define SX_DEBUG_OPEN          0x00000001
#define SX_DEBUG_SETTING       0x00000002
#define SX_DEBUG_FLOW          0x00000004
#define SX_DEBUG_MODEMSIGNALS  0x00000008
#define SX_DEBUG_TERMIOS       0x00000010
#define SX_DEBUG_TRANSMIT      0x00000020
#define SX_DEBUG_RECEIVE       0x00000040
#define SX_DEBUG_INTERRUPTS    0x00000080
#define SX_DEBUG_PROBE         0x00000100
#define SX_DEBUG_INIT          0x00000200
#define SX_DEBUG_CLEANUP       0x00000400
#define SX_DEBUG_CLOSE         0x00000800
#define SX_DEBUG_FIRMWARE      0x00001000
#define SX_DEBUG_MEMTEST       0x00002000

#define SX_DEBUG_ALL           0xffffffff


#define O_OTHER(tty)    \
      ((O_OLCUC(tty))  ||\
      (O_ONLCR(tty))   ||\
      (O_OCRNL(tty))   ||\
      (O_ONOCR(tty))   ||\
      (O_ONLRET(tty))  ||\
      (O_OFILL(tty))   ||\
      (O_OFDEL(tty))   ||\
      (O_NLDLY(tty))   ||\
      (O_CRDLY(tty))   ||\
      (O_TABDLY(tty))  ||\
      (O_BSDLY(tty))   ||\
      (O_VTDLY(tty))   ||\
      (O_FFDLY(tty)))

/* Same for input. */
#define I_OTHER(tty)    \
      ((I_INLCR(tty))  ||\
      (I_IGNCR(tty))   ||\
      (I_ICRNL(tty))   ||\
      (I_IUCLC(tty))   ||\
      (L_ISIG(tty)))

#define MOD_TA   (        TA>>4)
#define MOD_MTA  (MTA_CD1400>>4)
#define MOD_SXDC (      SXDC>>4)


/* We copy the download code over to the card in chunks of ... bytes */
#define SX_CHUNK_SIZE 128

#endif /* __KERNEL__ */



/* Specialix document 6210046-11 page 3 */
#define SPX(X) (('S'<<24) | ('P' << 16) | (X))

/* Specialix-Linux specific IOCTLS. */
#define SPXL(X) (SPX(('L' << 8) | (X)))


#define SXIO_SET_BOARD      SPXL(0x01)
#define SXIO_GET_TYPE       SPXL(0x02)
#define SXIO_DOWNLOAD       SPXL(0x03)
#define SXIO_INIT           SPXL(0x04)
#define SXIO_SETDEBUG       SPXL(0x05)
#define SXIO_GETDEBUG       SPXL(0x06)
#define SXIO_DO_RAMTEST     SPXL(0x07)
#define SXIO_SETGSDEBUG     SPXL(0x08)
#define SXIO_GETGSDEBUG     SPXL(0x09)
#define SXIO_GETNPORTS      SPXL(0x0a)


#ifndef SXCTL_MISC_MINOR 
/* Allow others to gather this into "major.h" or something like that */
#define SXCTL_MISC_MINOR    167
#endif

#ifndef SX_NORMAL_MAJOR
/* This allows overriding on the compiler commandline, or in a "major.h" 
   include or something like that */
#define SX_NORMAL_MAJOR  32
#define SX_CALLOUT_MAJOR 33
#endif


#define SX_TYPE_SX          0x01
#define SX_TYPE_SI          0x02
#define SX_TYPE_CF          0x03


#define WINDOW_LEN(board) (IS_CF_BOARD(board)?0x20000:SX_WINDOW_LEN)
/*                         Need a #define for ^^^^^^^ !!! */

