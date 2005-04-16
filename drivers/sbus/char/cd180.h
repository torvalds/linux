
/* Definitions for Cirrus Logic CL-CD180 8-port async mux chip */
#define CD180_NCH       8       /* Total number of channels                */
#define CD180_TPC       16      /* Ticks per character                     */
#define CD180_NFIFO	8	/* TX FIFO size                            */

/* Global registers */
#define CD180_GFRCR	0x6b	/* Global Firmware Revision Code Register  */
#define CD180_SRCR	0x66	/* Service Request Configuration Register  */
#define CD180_PPRH	0x70	/* Prescaler Period Register High	   */
#define CD180_PPRL	0x71	/* Prescaler Period Register Low	   */
#define CD180_MSMR	0x61	/* Modem Service Match Register		   */
#define CD180_TSMR	0x62	/* Transmit Service Match Register	   */
#define CD180_RSMR	0x63	/* Receive Service Match Register	   */
#define CD180_GSVR	0x40	/* Global Service Vector Register	   */
#define CD180_SRSR	0x65	/* Service Request Status Register	   */
#define CD180_GSCR	0x41	/* Global Service Channel Register	   */
#define CD180_CAR	0x64	/* Channel Access Register		   */

/* Indexed registers */
#define CD180_RDCR	0x07	/* Receive Data Count Register		   */
#define CD180_RDR	0x78	/* Receiver Data Register		   */
#define CD180_RCSR	0x7a	/* Receiver Character Status Register	   */
#define CD180_TDR	0x7b	/* Transmit Data Register		   */
#define CD180_EOSRR	0x7f	/* End of Service Request Register	   */

/* Channel Registers */
#define CD180_SRER      0x02    /* Service Request Enable Register         */
#define CD180_CCR       0x01    /* Channel Command Register                */
#define CD180_COR1      0x03    /* Channel Option Register 1               */
#define CD180_COR2      0x04    /* Channel Option Register 2               */
#define CD180_COR3      0x05    /* Channel Option Register 3               */
#define CD180_CCSR      0x06    /* Channel Control Status Register         */
#define CD180_RTPR      0x18    /* Receive Timeout Period Register         */
#define CD180_RBPRH     0x31    /* Receive Bit Rate Period Register High  */
#define CD180_RBPRL     0x32    /* Receive Bit Rate Period Register Low   */
#define CD180_TBPRH     0x39    /* Transmit Bit Rate Period Register High */
#define CD180_TBPRL     0x3a    /* Transmit Bit Rate Period Register Low  */
#define CD180_SCHR1     0x09    /* Special Character Register 1            */
#define CD180_SCHR2     0x0a    /* Special Character Register 2            */
#define CD180_SCHR3     0x0b    /* Special Character Register 3            */
#define CD180_SCHR4     0x0c    /* Special Character Register 4            */
#define CD180_MCR       0x12    /* Modem Change Register                   */
#define CD180_MCOR1     0x10    /* Modem Change Option 1 Register          */
#define CD180_MCOR2     0x11    /* Modem Change Option 2 Register          */
#define CD180_MSVR      0x28    /* Modem Signal Value Register             */
#define CD180_MSVRTS    0x29    /* Modem Signal Value RTS                  */
#define CD180_MSVDTR    0x2a    /* Modem Signal Value DTR                  */

/* Global Interrupt Vector Register (R/W) */

#define GSVR_ITMASK     0x07     /* Interrupt type mask                     */
#define  GSVR_IT_MDM     0x01    /* Modem Signal Change Interrupt           */
#define  GSVR_IT_TX      0x02    /* Transmit Data Interrupt                 */
#define  GSVR_IT_RGD     0x03    /* Receive Good Data Interrupt             */
#define  GSVR_IT_REXC    0x07    /* Receive Exception Interrupt             */


/* Global Interrupt Channel Register (R/W) */
 
#define GSCR_CHAN       0x1c    /* Channel Number Mask                     */
#define GSCR_CHAN_OFF   2       /* Channel Number Offset                   */


/* Channel Address Register (R/W) */

#define CAR_CHAN        0x07    /* Channel Number Mask                     */


/* Receive Character Status Register (R/O) */

#define RCSR_TOUT       0x80    /* Rx Timeout                              */
#define RCSR_SCDET      0x70    /* Special Character Detected Mask         */
#define  RCSR_NO_SC      0x00   /* No Special Characters Detected          */
#define  RCSR_SC_1       0x10   /* Special Char 1 (or 1 & 3) Detected      */
#define  RCSR_SC_2       0x20   /* Special Char 2 (or 2 & 4) Detected      */
#define  RCSR_SC_3       0x30   /* Special Char 3 Detected                 */
#define  RCSR_SC_4       0x40   /* Special Char 4 Detected                 */
#define RCSR_BREAK      0x08    /* Break has been detected                 */
#define RCSR_PE         0x04    /* Parity Error                            */
#define RCSR_FE         0x02    /* Frame Error                             */
#define RCSR_OE         0x01    /* Overrun Error                           */


/* Channel Command Register (R/W) (commands in groups can be OR-ed) */

#define CCR_HARDRESET   0x81    /* Reset the chip                          */

#define CCR_SOFTRESET   0x80    /* Soft Channel Reset                      */

#define CCR_CORCHG1     0x42    /* Channel Option Register 1 Changed       */
#define CCR_CORCHG2     0x44    /* Channel Option Register 2 Changed       */
#define CCR_CORCHG3     0x48    /* Channel Option Register 3 Changed       */

#define CCR_SSCH1       0x21    /* Send Special Character 1                */

#define CCR_SSCH2       0x22    /* Send Special Character 2                */

#define CCR_SSCH3       0x23    /* Send Special Character 3                */

#define CCR_SSCH4       0x24    /* Send Special Character 4                */

#define CCR_TXEN        0x18    /* Enable Transmitter                      */
#define CCR_RXEN        0x12    /* Enable Receiver                         */

#define CCR_TXDIS       0x14    /* Disable Transmitter                     */
#define CCR_RXDIS       0x11    /* Disable Receiver                        */


/* Service Request Enable Register (R/W) */

#define SRER_DSR         0x80    /* Enable interrupt on DSR change          */
#define SRER_CD          0x40    /* Enable interrupt on CD change           */
#define SRER_CTS         0x20    /* Enable interrupt on CTS change          */
#define SRER_RXD         0x10    /* Enable interrupt on Receive Data        */
#define SRER_RXSC        0x08    /* Enable interrupt on Receive Spec. Char  */
#define SRER_TXRDY       0x04    /* Enable interrupt on TX FIFO empty       */
#define SRER_TXEMPTY     0x02    /* Enable interrupt on TX completely empty */
#define SRER_RET         0x01    /* Enable interrupt on RX Exc. Timeout     */


/* Channel Option Register 1 (R/W) */

#define COR1_ODDP       0x80    /* Odd Parity                              */
#define COR1_PARMODE    0x60    /* Parity Mode mask                        */
#define  COR1_NOPAR      0x00   /* No Parity                               */
#define  COR1_FORCEPAR   0x20   /* Force Parity                            */
#define  COR1_NORMPAR    0x40   /* Normal Parity                           */
#define COR1_IGNORE     0x10    /* Ignore Parity on RX                     */
#define COR1_STOPBITS   0x0c    /* Number of Stop Bits                     */
#define  COR1_1SB        0x00   /* 1 Stop Bit                              */
#define  COR1_15SB       0x04   /* 1.5 Stop Bits                           */
#define  COR1_2SB        0x08   /* 2 Stop Bits                             */
#define COR1_CHARLEN    0x03    /* Character Length                        */
#define  COR1_5BITS      0x00   /* 5 bits                                  */
#define  COR1_6BITS      0x01   /* 6 bits                                  */
#define  COR1_7BITS      0x02   /* 7 bits                                  */
#define  COR1_8BITS      0x03   /* 8 bits                                  */


/* Channel Option Register 2 (R/W) */

#define COR2_IXM        0x80    /* Implied XON mode                        */
#define COR2_TXIBE      0x40    /* Enable In-Band (XON/XOFF) Flow Control  */
#define COR2_ETC        0x20    /* Embedded Tx Commands Enable             */
#define COR2_LLM        0x10    /* Local Loopback Mode                     */
#define COR2_RLM        0x08    /* Remote Loopback Mode                    */
#define COR2_RTSAO      0x04    /* RTS Automatic Output Enable             */
#define COR2_CTSAE      0x02    /* CTS Automatic Enable                    */
#define COR2_DSRAE      0x01    /* DSR Automatic Enable                    */


/* Channel Option Register 3 (R/W) */

#define COR3_XONCH      0x80    /* XON is a pair of characters (1 & 3)     */
#define COR3_XOFFCH     0x40    /* XOFF is a pair of characters (2 & 4)    */
#define COR3_FCT        0x20    /* Flow-Control Transparency Mode          */
#define COR3_SCDE       0x10    /* Special Character Detection Enable      */
#define COR3_RXTH       0x0f    /* RX FIFO Threshold value (1-8)           */


/* Channel Control Status Register (R/O) */

#define CCSR_RXEN       0x80    /* Receiver Enabled                        */
#define CCSR_RXFLOFF    0x40    /* Receive Flow Off (XOFF was sent)        */
#define CCSR_RXFLON     0x20    /* Receive Flow On (XON was sent)          */
#define CCSR_TXEN       0x08    /* Transmitter Enabled                     */
#define CCSR_TXFLOFF    0x04    /* Transmit Flow Off (got XOFF)            */
#define CCSR_TXFLON     0x02    /* Transmit Flow On (got XON)              */


/* Modem Change Option Register 1 (R/W) */

#define MCOR1_DSRZD     0x80    /* Detect 0->1 transition of DSR           */
#define MCOR1_CDZD      0x40    /* Detect 0->1 transition of CD            */
#define MCOR1_CTSZD     0x20    /* Detect 0->1 transition of CTS           */
#define MCOR1_DTRTH     0x0f    /* Auto DTR flow control Threshold (1-8)   */
#define  MCOR1_NODTRFC   0x0     /* Automatic DTR flow control disabled     */


/* Modem Change Option Register 2 (R/W) */

#define MCOR2_DSROD     0x80    /* Detect 1->0 transition of DSR           */
#define MCOR2_CDOD      0x40    /* Detect 1->0 transition of CD            */
#define MCOR2_CTSOD     0x20    /* Detect 1->0 transition of CTS           */


/* Modem Change Register (R/W) */

#define MCR_DSRCHG      0x80    /* DSR Changed                             */
#define MCR_CDCHG       0x40    /* CD Changed                              */
#define MCR_CTSCHG      0x20    /* CTS Changed                             */


/* Modem Signal Value Register (R/W) */

#define MSVR_DSR        0x80    /* Current state of DSR input              */
#define MSVR_CD         0x40    /* Current state of CD input               */
#define MSVR_CTS        0x20    /* Current state of CTS input              */
#define MSVR_DTR        0x02    /* Current state of DTR output             */
#define MSVR_RTS        0x01    /* Current state of RTS output             */


/* Service Request Status Register */

#define SRSR_CMASK	0xC0	/* Current Service Context Mask            */
#define  SRSR_CNONE	0x00	/* Not in a service context		   */
#define  SRSR_CRX	0x40	/* Rx Context				   */
#define  SRSR_CTX	0x80	/* Tx Context				   */
#define  SRSR_CMDM	0xC0	/* Modem Context			   */
#define SRSR_ANYINT	0x6F	/* Any interrupt flag			   */
#define SRSR_RINT	0x10	/* Receive Interrupt			   */
#define SRSR_TINT	0x04	/* Transmit Interrupt			   */
#define SRSR_MINT	0x01	/* Modem Interrupt			   */
#define SRSR_REXT	0x20	/* Receive External Interrupt		   */
#define SRSR_TEXT	0x08	/* Transmit External Interrupt		   */
#define SRSR_MEXT	0x02	/* Modem External Interrupt		   */


/* Service Request Configuration Register */

#define SRCR_PKGTYPE    0x80
#define SRCR_REGACKEN   0x40
#define SRCR_DAISYEN    0x20
#define SRCR_GLOBPRI    0x10
#define SRCR_UNFAIR     0x08
#define SRCR_AUTOPRI    0x02
#define SRCR_PRISEL     0x01

/* Values for register-based Interrupt ACKs */
#define CD180_ACK_MINT	0x75	/* goes to MSMR				   */
#define CD180_ACK_TINT	0x76	/* goes to TSMR				   */
#define CD180_ACK_RINT	0x77	/* goes to RSMR				   */

/* Escape characters */

#define CD180_C_ESC     0x00    /* Escape character                        */
#define CD180_C_SBRK    0x81    /* Start sending BREAK                     */
#define CD180_C_DELAY   0x82    /* Delay output                            */
#define CD180_C_EBRK    0x83    /* Stop sending BREAK                      */
