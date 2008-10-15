/* internal Peripherals Register address define */
/* CPU: H8/306x                                 */

#if !defined(__REGS_H8306x__)
#define __REGS_H8306x__ 

#if defined(__KERNEL__)

#define DASTCR 0xFEE01A
#define DADR0  0xFEE09C
#define DADR1  0xFEE09D
#define DACR   0xFEE09E

#define ADDRAH 0xFFFFE0
#define ADDRAL 0xFFFFE1
#define ADDRBH 0xFFFFE2
#define ADDRBL 0xFFFFE3
#define ADDRCH 0xFFFFE4
#define ADDRCL 0xFFFFE5
#define ADDRDH 0xFFFFE6
#define ADDRDL 0xFFFFE7
#define ADCSR  0xFFFFE8
#define ADCR   0xFFFFE9

#define BRCR   0xFEE013
#define ADRCR  0xFEE01E
#define CSCR   0xFEE01F
#define ABWCR  0xFEE020
#define ASTCR  0xFEE021
#define WCRH   0xFEE022
#define WCRL   0xFEE023
#define BCR    0xFEE024
#define DRCRA  0xFEE026
#define DRCRB  0xFEE027
#define RTMCSR 0xFEE028
#define RTCNT  0xFEE029
#define RTCOR  0xFEE02A

#define MAR0AR  0xFFFF20
#define MAR0AE  0xFFFF21
#define MAR0AH  0xFFFF22
#define MAR0AL  0xFFFF23
#define ETCR0AL 0xFFFF24
#define ETCR0AH 0xFFFF25
#define IOAR0A  0xFFFF26
#define DTCR0A  0xFFFF27
#define MAR0BR  0xFFFF28
#define MAR0BE  0xFFFF29
#define MAR0BH  0xFFFF2A
#define MAR0BL  0xFFFF2B
#define ETCR0BL 0xFFFF2C
#define ETCR0BH 0xFFFF2D
#define IOAR0B  0xFFFF2E
#define DTCR0B  0xFFFF2F
#define MAR1AR  0xFFFF30
#define MAR1AE  0xFFFF31
#define MAR1AH  0xFFFF32
#define MAR1AL  0xFFFF33
#define ETCR1AL 0xFFFF34
#define ETCR1AH 0xFFFF35
#define IOAR1A  0xFFFF36
#define DTCR1A  0xFFFF37
#define MAR1BR  0xFFFF38
#define MAR1BE  0xFFFF39
#define MAR1BH  0xFFFF3A
#define MAR1BL  0xFFFF3B
#define ETCR1BL 0xFFFF3C
#define ETCR1BH 0xFFFF3D
#define IOAR1B  0xFFFF3E
#define DTCR1B  0xFFFF3F

#define ISCR 0xFEE014
#define IER  0xFEE015
#define ISR  0xFEE016
#define IPRA 0xFEE018
#define IPRB 0xFEE019

#define P1DDR 0xFEE000
#define P2DDR 0xFEE001
#define P3DDR 0xFEE002
#define P4DDR 0xFEE003
#define P5DDR 0xFEE004
#define P6DDR 0xFEE005
/*#define P7DDR 0xFEE006*/
#define P8DDR 0xFEE007
#define P9DDR 0xFEE008
#define PADDR 0xFEE009
#define PBDDR 0xFEE00A

#define P1DR  0xFFFFD0
#define P2DR  0xFFFFD1
#define P3DR  0xFFFFD2
#define P4DR  0xFFFFD3
#define P5DR  0xFFFFD4
#define P6DR  0xFFFFD5
/*#define P7DR  0xFFFFD6*/
#define P8DR  0xFFFFD7
#define P9DR  0xFFFFD8
#define PADR  0xFFFFD9
#define PBDR  0xFFFFDA

#define P2CR  0xFEE03C
#define P4CR  0xFEE03E
#define P5CR  0xFEE03F

#define SMR0  0xFFFFB0
#define BRR0  0xFFFFB1
#define SCR0  0xFFFFB2
#define TDR0  0xFFFFB3
#define SSR0  0xFFFFB4
#define RDR0  0xFFFFB5
#define SCMR0 0xFFFFB6
#define SMR1  0xFFFFB8
#define BRR1  0xFFFFB9
#define SCR1  0xFFFFBA
#define TDR1  0xFFFFBB
#define SSR1  0xFFFFBC
#define RDR1  0xFFFFBD
#define SCMR1 0xFFFFBE
#define SMR2  0xFFFFC0
#define BRR2  0xFFFFC1
#define SCR2  0xFFFFC2
#define TDR2  0xFFFFC3
#define SSR2  0xFFFFC4
#define RDR2  0xFFFFC5
#define SCMR2 0xFFFFC6

#define MDCR   0xFEE011
#define SYSCR  0xFEE012
#define DIVCR  0xFEE01B
#define MSTCRH 0xFEE01C
#define MSTCRL 0xFEE01D
#define FLMCR1 0xFEE030
#define FLMCR2 0xFEE031
#define EBR1   0xFEE032
#define EBR2   0xFEE033
#define RAMCR  0xFEE077

#define TSTR   0xFFFF60
#define TSNC   0XFFFF61
#define TMDR   0xFFFF62
#define TOLR   0xFFFF63
#define TISRA  0xFFFF64
#define TISRB  0xFFFF65
#define TISRC  0xFFFF66
#define TCR0   0xFFFF68
#define TIOR0  0xFFFF69
#define TCNT0H 0xFFFF6A
#define TCNT0L 0xFFFF6B
#define GRA0H  0xFFFF6C
#define GRA0L  0xFFFF6D
#define GRB0H  0xFFFF6E
#define GRB0L  0xFFFF6F
#define TCR1   0xFFFF70
#define TIOR1  0xFFFF71
#define TCNT1H 0xFFFF72
#define TCNT1L 0xFFFF73
#define GRA1H  0xFFFF74
#define GRA1L  0xFFFF75
#define GRB1H  0xFFFF76
#define GRB1L  0xFFFF77
#define TCR3   0xFFFF78
#define TIOR3  0xFFFF79
#define TCNT3H 0xFFFF7A
#define TCNT3L 0xFFFF7B
#define GRA3H  0xFFFF7C
#define GRA3L  0xFFFF7D
#define GRB3H  0xFFFF7E
#define GRB3L  0xFFFF7F

#define _8TCR0  0xFFFF80
#define _8TCR1  0xFFFF81
#define _8TCSR0 0xFFFF82
#define _8TCSR1 0xFFFF83
#define TCORA0 0xFFFF84
#define TCORA1 0xFFFF85
#define TCORB0 0xFFFF86
#define TCORB1 0xFFFF87
#define _8TCNT0 0xFFFF88
#define _8TCNT1 0xFFFF89

#define _8TCR2  0xFFFF90
#define _8TCR3  0xFFFF91
#define _8TCSR2 0xFFFF92
#define _8TCSR3 0xFFFF93
#define TCORA2 0xFFFF94
#define TCORA3 0xFFFF95
#define TCORB2 0xFFFF96
#define TCORB3 0xFFFF97
#define _8TCNT2 0xFFFF98
#define _8TCNT3 0xFFFF99

#define TCSR   0xFFFF8C
#define TCNT   0xFFFF8D
#define RSTCSR 0xFFFF8F

#define TPMR  0xFFFFA0
#define TPCR  0xFFFFA1
#define NDERB 0xFFFFA2
#define NDERA 0xFFFFA3
#define NDRB1 0xFFFFA4
#define NDRA1 0xFFFFA5
#define NDRB2 0xFFFFA6
#define NDRA2 0xFFFFA7

#define TCSR    0xFFFF8C
#define TCNT    0xFFFF8D
#define RSTCSRW 0xFFFF8E
#define RSTCSRR 0xFFFF8F

#endif /* __KERNEL__ */
#endif /* __REGS_H8306x__ */
