#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/gpio.h>

#if defined(CONFIG_H83007) || defined(CONFIG_H83068)
#include <asm/regs306x.h>
#endif
#if defined(CONFIG_H8S2678)
#include <asm/regs267x.h>
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7708) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
# define SCPCR  0xA4000116 /* 16 bit SCI and SCIF */
# define SCPDR  0xA4000136 /* 8  bit SCI and SCIF */
# define SCSCR_INIT(port)          0x30 /* TIE=0,RIE=0,TE=1,RE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7705)
# define SCIF0		0xA4400000
# define SCIF2		0xA4410000
# define SCSMR_Ir	0xA44A0000
# define IRDA_SCIF	SCIF0
# define SCPCR 0xA4000116
# define SCPDR 0xA4000136

/* Set the clock source,
 * SCIF2 (0xA4410000) -> External clock, SCK pin used as clock input
 * SCIF0 (0xA4400000) -> Internal clock, SCK pin as serial clock output
 */
# define SCSCR_INIT(port) (port->mapbase == SCIF2) ? 0xF3 : 0xF0
#elif defined(CONFIG_CPU_SUBTYPE_SH7720) || \
      defined(CONFIG_CPU_SUBTYPE_SH7721) || \
      defined(CONFIG_ARCH_SH73A0) || \
      defined(CONFIG_ARCH_SH7367) || \
      defined(CONFIG_ARCH_SH7377) || \
      defined(CONFIG_ARCH_SH7372)
# define SCSCR_INIT(port)  0x0030 /* TIE=0,RIE=0,TE=1,RE=1 */
# define PORT_PTCR	   0xA405011EUL
# define PORT_PVCR	   0xA4050122UL
# define SCIF_ORER	   0x0200   /* overrun error bit */
#elif defined(CONFIG_SH_RTS7751R2D)
# define SCSPTR1 0xFFE0001C /* 8 bit SCIF */
# define SCSPTR2 0xFFE80020 /* 16 bit SCIF */
# define SCIF_ORER 0x0001   /* overrun error bit */
# define SCSCR_INIT(port) 0x3a /* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7750)  || \
      defined(CONFIG_CPU_SUBTYPE_SH7750R) || \
      defined(CONFIG_CPU_SUBTYPE_SH7750S) || \
      defined(CONFIG_CPU_SUBTYPE_SH7091)  || \
      defined(CONFIG_CPU_SUBTYPE_SH7751)  || \
      defined(CONFIG_CPU_SUBTYPE_SH7751R)
# define SCSPTR1 0xffe0001c /* 8  bit SCI */
# define SCSPTR2 0xFFE80020 /* 16 bit SCIF */
# define SCIF_ORER 0x0001   /* overrun error bit */
# define SCSCR_INIT(port) (((port)->type == PORT_SCI) ? \
	0x30 /* TIE=0,RIE=0,TE=1,RE=1 */ : \
	0x38 /* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */ )
#elif defined(CONFIG_CPU_SUBTYPE_SH7760)
# define SCSPTR0 0xfe600024 /* 16 bit SCIF */
# define SCSPTR1 0xfe610024 /* 16 bit SCIF */
# define SCSPTR2 0xfe620024 /* 16 bit SCIF */
# define SCIF_ORER 0x0001  /* overrun error bit */
# define SCSCR_INIT(port)          0x38 /* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7710) || defined(CONFIG_CPU_SUBTYPE_SH7712)
# define SCSPTR0 0xA4400000	  /* 16 bit SCIF */
# define SCIF_ORER 0x0001   /* overrun error bit */
# define PACR 0xa4050100
# define PBCR 0xa4050102
# define SCSCR_INIT(port)          0x3B
#elif defined(CONFIG_CPU_SUBTYPE_SH7343)
# define SCSPTR0 0xffe00010	/* 16 bit SCIF */
# define SCSPTR1 0xffe10010	/* 16 bit SCIF */
# define SCSPTR2 0xffe20010	/* 16 bit SCIF */
# define SCSPTR3 0xffe30010	/* 16 bit SCIF */
# define SCSCR_INIT(port) 0x32	/* TIE=0,RIE=0,TE=1,RE=1,REIE=0,CKE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7722)
# define PADR			0xA4050120
# define PSDR			0xA405013e
# define PWDR			0xA4050166
# define PSCR			0xA405011E
# define SCIF_ORER		0x0001	/* overrun error bit */
# define SCSCR_INIT(port)	0x0038	/* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7366)
# define SCPDR0			0xA405013E      /* 16 bit SCIF0 PSDR */
# define SCSPTR0		SCPDR0
# define SCIF_ORER		0x0001  /* overrun error bit */
# define SCSCR_INIT(port)	0x0038  /* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7723)
# define SCSPTR0                0xa4050160
# define SCSPTR1                0xa405013e
# define SCSPTR2                0xa4050160
# define SCSPTR3                0xa405013e
# define SCSPTR4                0xa4050128
# define SCSPTR5                0xa4050128
# define SCIF_ORER              0x0001  /* overrun error bit */
# define SCSCR_INIT(port)       0x0038  /* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7724)
# define SCIF_ORER              0x0001  /* overrun error bit */
# define SCSCR_INIT(port) ((port)->type == PORT_SCIFA ? \
	0x30 /* TIE=0,RIE=0,TE=1,RE=1 */ : \
	0x38 /* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */ )
#elif defined(CONFIG_CPU_SUBTYPE_SH4_202)
# define SCSPTR2 0xffe80020 /* 16 bit SCIF */
# define SCIF_ORER 0x0001   /* overrun error bit */
# define SCSCR_INIT(port) 0x38 /* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH5_101) || defined(CONFIG_CPU_SUBTYPE_SH5_103)
# define SCIF_BASE_ADDR    0x01030000
# define SCIF_ADDR_SH5     PHYS_PERIPHERAL_BLOCK+SCIF_BASE_ADDR
# define SCIF_PTR2_OFFS    0x0000020
# define SCIF_LSR2_OFFS    0x0000024
# define SCSPTR2           ((port->mapbase)+SCIF_PTR2_OFFS) /* 16 bit SCIF */
# define SCLSR2            ((port->mapbase)+SCIF_LSR2_OFFS) /* 16 bit SCIF */
# define SCSCR_INIT(port)  0x38		/* TIE=0,RIE=0, TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_H83007) || defined(CONFIG_H83068)
# define SCSCR_INIT(port)          0x30 /* TIE=0,RIE=0,TE=1,RE=1 */
# define H8300_SCI_DR(ch) *(volatile char *)(P1DR + h8300_sci_pins[ch].port)
#elif defined(CONFIG_H8S2678)
# define SCSCR_INIT(port)          0x30 /* TIE=0,RIE=0,TE=1,RE=1 */
# define H8300_SCI_DR(ch) *(volatile char *)(P1DR + h8300_sci_pins[ch].port)
#elif defined(CONFIG_CPU_SUBTYPE_SH7757)
# define SCSPTR0 0xfe4b0020
# define SCSPTR1 0xfe4b0020
# define SCSPTR2 0xfe4b0020
# define SCIF_ORER 0x0001
# define SCSCR_INIT(port)	0x38
# define SCIF_ONLY
#elif defined(CONFIG_CPU_SUBTYPE_SH7763)
# define SCSPTR0 0xffe00024 /* 16 bit SCIF */
# define SCSPTR1 0xffe08024 /* 16 bit SCIF */
# define SCSPTR2 0xffe10020 /* 16 bit SCIF/IRDA */
# define SCIF_ORER 0x0001  /* overrun error bit */
# define SCSCR_INIT(port)	0x38	/* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7770)
# define SCSPTR0 0xff923020 /* 16 bit SCIF */
# define SCSPTR1 0xff924020 /* 16 bit SCIF */
# define SCSPTR2 0xff925020 /* 16 bit SCIF */
# define SCIF_ORER 0x0001  /* overrun error bit */
# define SCSCR_INIT(port)	0x3c /* TIE=0,RIE=0,TE=1,RE=1,REIE=1,cke=2 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7780)
# define SCSPTR0	0xffe00024	/* 16 bit SCIF */
# define SCSPTR1	0xffe10024	/* 16 bit SCIF */
# define SCIF_ORER	0x0001		/* Overrun error bit */

#if defined(CONFIG_SH_SH2007)
/* TIE=0,RIE=0,TE=1,RE=1,REIE=1,CKE1=0 */
# define SCSCR_INIT(port)	0x38
#else
/* TIE=0,RIE=0,TE=1,RE=1,REIE=1,CKE1=1 */
# define SCSCR_INIT(port)	0x3a
#endif

#elif defined(CONFIG_CPU_SUBTYPE_SH7785) || \
      defined(CONFIG_CPU_SUBTYPE_SH7786)
# define SCSPTR0	0xffea0024	/* 16 bit SCIF */
# define SCSPTR1	0xffeb0024	/* 16 bit SCIF */
# define SCSPTR2	0xffec0024	/* 16 bit SCIF */
# define SCSPTR3	0xffed0024	/* 16 bit SCIF */
# define SCSPTR4	0xffee0024	/* 16 bit SCIF */
# define SCSPTR5	0xffef0024	/* 16 bit SCIF */
# define SCIF_ORER	0x0001		/* Overrun error bit */
# define SCSCR_INIT(port)	0x3a	/* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7201) || \
      defined(CONFIG_CPU_SUBTYPE_SH7203) || \
      defined(CONFIG_CPU_SUBTYPE_SH7206) || \
      defined(CONFIG_CPU_SUBTYPE_SH7263)
# define SCSPTR0 0xfffe8020 /* 16 bit SCIF */
# define SCSPTR1 0xfffe8820 /* 16 bit SCIF */
# define SCSPTR2 0xfffe9020 /* 16 bit SCIF */
# define SCSPTR3 0xfffe9820 /* 16 bit SCIF */
# if defined(CONFIG_CPU_SUBTYPE_SH7201)
#  define SCSPTR4 0xfffeA020 /* 16 bit SCIF */
#  define SCSPTR5 0xfffeA820 /* 16 bit SCIF */
#  define SCSPTR6 0xfffeB020 /* 16 bit SCIF */
#  define SCSPTR7 0xfffeB820 /* 16 bit SCIF */
# endif
# define SCSCR_INIT(port)	0x38 /* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7619)
# define SCSPTR0 0xf8400020 /* 16 bit SCIF */
# define SCSPTR1 0xf8410020 /* 16 bit SCIF */
# define SCSPTR2 0xf8420020 /* 16 bit SCIF */
# define SCIF_ORER 0x0001  /* overrun error bit */
# define SCSCR_INIT(port)	0x38 /* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#elif defined(CONFIG_CPU_SUBTYPE_SHX3)
# define SCSPTR0 0xffc30020		/* 16 bit SCIF */
# define SCSPTR1 0xffc40020		/* 16 bit SCIF */
# define SCSPTR2 0xffc50020		/* 16 bit SCIF */
# define SCSPTR3 0xffc60020		/* 16 bit SCIF */
# define SCIF_ORER 0x0001		/* Overrun error bit */
# define SCSCR_INIT(port)	0x38	/* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#else
# error CPU subtype not defined
#endif

/* SCSCR */
#define SCI_CTRL_FLAGS_TIE  0x80 /* all */
#define SCI_CTRL_FLAGS_RIE  0x40 /* all */
#define SCI_CTRL_FLAGS_TE   0x20 /* all */
#define SCI_CTRL_FLAGS_RE   0x10 /* all */
#if defined(CONFIG_CPU_SUBTYPE_SH7750)  || \
    defined(CONFIG_CPU_SUBTYPE_SH7091)  || \
    defined(CONFIG_CPU_SUBTYPE_SH7750R) || \
    defined(CONFIG_CPU_SUBTYPE_SH7722)  || \
    defined(CONFIG_CPU_SUBTYPE_SH7750S) || \
    defined(CONFIG_CPU_SUBTYPE_SH7751)  || \
    defined(CONFIG_CPU_SUBTYPE_SH7751R) || \
    defined(CONFIG_CPU_SUBTYPE_SH7763)  || \
    defined(CONFIG_CPU_SUBTYPE_SH7780)  || \
    defined(CONFIG_CPU_SUBTYPE_SH7785)  || \
    defined(CONFIG_CPU_SUBTYPE_SH7786)  || \
    defined(CONFIG_CPU_SUBTYPE_SHX3)
#define SCI_CTRL_FLAGS_REIE 0x08 /* 7750 SCIF */
#elif defined(CONFIG_CPU_SUBTYPE_SH7724)
#define SCI_CTRL_FLAGS_REIE ((port)->type == PORT_SCIFA ? 0 : 8)
#else
#define SCI_CTRL_FLAGS_REIE 0
#endif
/*      SCI_CTRL_FLAGS_MPIE 0x08  * 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */
/*      SCI_CTRL_FLAGS_TEIE 0x04  * 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */
/*      SCI_CTRL_FLAGS_CKE1 0x02  * all */
/*      SCI_CTRL_FLAGS_CKE0 0x01  * 7707 SCI/SCIF, 7708 SCI, 7709 SCI/SCIF, 7750 SCI */

/* SCxSR SCI */
#define SCI_TDRE  0x80 /* 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */
#define SCI_RDRF  0x40 /* 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */
#define SCI_ORER  0x20 /* 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */
#define SCI_FER   0x10 /* 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */
#define SCI_PER   0x08 /* 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */
#define SCI_TEND  0x04 /* 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */
/*      SCI_MPB   0x02  * 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */
/*      SCI_MPBT  0x01  * 7707 SCI, 7708 SCI, 7709 SCI, 7750 SCI */

#define SCI_ERRORS ( SCI_PER | SCI_FER | SCI_ORER)

/* SCxSR SCIF */
#define SCIF_ER    0x0080 /* 7705 SCIF, 7707 SCIF, 7709 SCIF, 7750 SCIF */
#define SCIF_TEND  0x0040 /* 7705 SCIF, 7707 SCIF, 7709 SCIF, 7750 SCIF */
#define SCIF_TDFE  0x0020 /* 7705 SCIF, 7707 SCIF, 7709 SCIF, 7750 SCIF */
#define SCIF_BRK   0x0010 /* 7705 SCIF, 7707 SCIF, 7709 SCIF, 7750 SCIF */
#define SCIF_FER   0x0008 /* 7705 SCIF, 7707 SCIF, 7709 SCIF, 7750 SCIF */
#define SCIF_PER   0x0004 /* 7705 SCIF, 7707 SCIF, 7709 SCIF, 7750 SCIF */
#define SCIF_RDF   0x0002 /* 7705 SCIF, 7707 SCIF, 7709 SCIF, 7750 SCIF */
#define SCIF_DR    0x0001 /* 7705 SCIF, 7707 SCIF, 7709 SCIF, 7750 SCIF */

#if defined(CONFIG_CPU_SUBTYPE_SH7705) || \
    defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721) || \
    defined(CONFIG_ARCH_SH73A0) || \
    defined(CONFIG_ARCH_SH7367) || \
    defined(CONFIG_ARCH_SH7377) || \
    defined(CONFIG_ARCH_SH7372)
# define SCIF_ORER    0x0200
# define SCIF_ERRORS ( SCIF_PER | SCIF_FER | SCIF_ER | SCIF_BRK | SCIF_ORER)
# define SCIF_RFDC_MASK 0x007f
# define SCIF_TXROOM_MAX 64
#elif defined(CONFIG_CPU_SUBTYPE_SH7763)
# define SCIF_ERRORS ( SCIF_PER | SCIF_FER | SCIF_ER | SCIF_BRK )
# define SCIF_RFDC_MASK 0x007f
# define SCIF_TXROOM_MAX 64
/* SH7763 SCIF2 support */
# define SCIF2_RFDC_MASK 0x001f
# define SCIF2_TXROOM_MAX 16
#else
# define SCIF_ERRORS ( SCIF_PER | SCIF_FER | SCIF_ER | SCIF_BRK)
# define SCIF_RFDC_MASK 0x001f
# define SCIF_TXROOM_MAX 16
#endif

#ifndef SCIF_ORER
#define SCIF_ORER	0x0000
#endif

#define SCxSR_TEND(port)	(((port)->type == PORT_SCI) ? SCI_TEND   : SCIF_TEND)
#define SCxSR_ERRORS(port)	(((port)->type == PORT_SCI) ? SCI_ERRORS : SCIF_ERRORS)
#define SCxSR_RDxF(port)	(((port)->type == PORT_SCI) ? SCI_RDRF   : SCIF_RDF)
#define SCxSR_TDxE(port)	(((port)->type == PORT_SCI) ? SCI_TDRE   : SCIF_TDFE)
#define SCxSR_FER(port)		(((port)->type == PORT_SCI) ? SCI_FER    : SCIF_FER)
#define SCxSR_PER(port)		(((port)->type == PORT_SCI) ? SCI_PER    : SCIF_PER)
#define SCxSR_BRK(port)		(((port)->type == PORT_SCI) ? 0x00       : SCIF_BRK)
#define SCxSR_ORER(port)	(((port)->type == PORT_SCI) ? SCI_ORER	 : SCIF_ORER)

#if defined(CONFIG_CPU_SUBTYPE_SH7705) || \
    defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721) || \
    defined(CONFIG_ARCH_SH73A0) || \
    defined(CONFIG_ARCH_SH7367) || \
    defined(CONFIG_ARCH_SH7377) || \
    defined(CONFIG_ARCH_SH7372)
# define SCxSR_RDxF_CLEAR(port)	 (sci_in(port, SCxSR) & 0xfffc)
# define SCxSR_ERROR_CLEAR(port) (sci_in(port, SCxSR) & 0xfd73)
# define SCxSR_TDxE_CLEAR(port)	 (sci_in(port, SCxSR) & 0xffdf)
# define SCxSR_BREAK_CLEAR(port) (sci_in(port, SCxSR) & 0xffe3)
#else
# define SCxSR_RDxF_CLEAR(port)	 (((port)->type == PORT_SCI) ? 0xbc : 0x00fc)
# define SCxSR_ERROR_CLEAR(port) (((port)->type == PORT_SCI) ? 0xc4 : 0x0073)
# define SCxSR_TDxE_CLEAR(port)  (((port)->type == PORT_SCI) ? 0x78 : 0x00df)
# define SCxSR_BREAK_CLEAR(port) (((port)->type == PORT_SCI) ? 0xc4 : 0x00e3)
#endif

/* SCFCR */
#define SCFCR_RFRST 0x0002
#define SCFCR_TFRST 0x0004
#define SCFCR_TCRST 0x4000
#define SCFCR_MCE   0x0008

#define SCI_MAJOR		204
#define SCI_MINOR_START		8

/* Generic serial flags */
#define SCI_RX_THROTTLE		0x0000001

#define SCI_MAGIC 0xbabeface

/*
 * Events are used to schedule things to happen at timer-interrupt
 * time, instead of at rs interrupt time.
 */
#define SCI_EVENT_WRITE_WAKEUP	0

#define SCI_IN(size, offset)					\
  if ((size) == 8) {						\
    return ioread8(port->membase + (offset));			\
  } else {							\
    return ioread16(port->membase + (offset));			\
  }
#define SCI_OUT(size, offset, value)				\
  if ((size) == 8) {						\
    iowrite8(value, port->membase + (offset));			\
  } else if ((size) == 16) {					\
    iowrite16(value, port->membase + (offset));			\
  }

#define CPU_SCIx_FNS(name, sci_offset, sci_size, scif_offset, scif_size)\
  static inline unsigned int sci_##name##_in(struct uart_port *port)	\
  {									\
    if (port->type == PORT_SCIF || port->type == PORT_SCIFB) {		\
      SCI_IN(scif_size, scif_offset)					\
    } else {	/* PORT_SCI or PORT_SCIFA */				\
      SCI_IN(sci_size, sci_offset);					\
    }									\
  }									\
  static inline void sci_##name##_out(struct uart_port *port, unsigned int value) \
  {									\
    if (port->type == PORT_SCIF || port->type == PORT_SCIFB) {		\
      SCI_OUT(scif_size, scif_offset, value)				\
    } else {	/* PORT_SCI or PORT_SCIFA */				\
      SCI_OUT(sci_size, sci_offset, value);				\
    }									\
  }

#ifdef CONFIG_H8300
/* h8300 don't have SCIF */
#define CPU_SCIF_FNS(name)						\
  static inline unsigned int sci_##name##_in(struct uart_port *port)	\
  {									\
    return 0;								\
  }									\
  static inline void sci_##name##_out(struct uart_port *port, unsigned int value) \
  {									\
  }
#else
#define CPU_SCIF_FNS(name, scif_offset, scif_size)			\
  static inline unsigned int sci_##name##_in(struct uart_port *port)	\
  {									\
    SCI_IN(scif_size, scif_offset);					\
  }									\
  static inline void sci_##name##_out(struct uart_port *port, unsigned int value) \
  {									\
    SCI_OUT(scif_size, scif_offset, value);				\
  }
#endif

#define CPU_SCI_FNS(name, sci_offset, sci_size)				\
  static inline unsigned int sci_##name##_in(struct uart_port* port)	\
  {									\
    SCI_IN(sci_size, sci_offset);					\
  }									\
  static inline void sci_##name##_out(struct uart_port* port, unsigned int value) \
  {									\
    SCI_OUT(sci_size, sci_offset, value);				\
  }

#if defined(CONFIG_CPU_SH3) || \
    defined(CONFIG_ARCH_SH73A0) || \
    defined(CONFIG_ARCH_SH7367) || \
    defined(CONFIG_ARCH_SH7377) || \
    defined(CONFIG_ARCH_SH7372)
#if defined(CONFIG_CPU_SUBTYPE_SH7710) || defined(CONFIG_CPU_SUBTYPE_SH7712)
#define SCIx_FNS(name, sh3_sci_offset, sh3_sci_size, sh4_sci_offset, sh4_sci_size, \
		                sh3_scif_offset, sh3_scif_size, sh4_scif_offset, sh4_scif_size, \
		                 h8_sci_offset, h8_sci_size) \
  CPU_SCIx_FNS(name, sh4_sci_offset, sh4_sci_size, sh4_scif_offset, sh4_scif_size)
#define SCIF_FNS(name, sh3_scif_offset, sh3_scif_size, sh4_scif_offset, sh4_scif_size) \
	  CPU_SCIF_FNS(name, sh4_scif_offset, sh4_scif_size)
#elif defined(CONFIG_CPU_SUBTYPE_SH7705) || \
      defined(CONFIG_CPU_SUBTYPE_SH7720) || \
      defined(CONFIG_CPU_SUBTYPE_SH7721) || \
      defined(CONFIG_ARCH_SH73A0) || \
      defined(CONFIG_ARCH_SH7367) || \
      defined(CONFIG_ARCH_SH7377)
#define SCIF_FNS(name, scif_offset, scif_size) \
  CPU_SCIF_FNS(name, scif_offset, scif_size)
#elif defined(CONFIG_ARCH_SH7372)
#define SCIx_FNS(name, sh4_scifa_offset, sh4_scifa_size, sh4_scifb_offset, sh4_scifb_size) \
  CPU_SCIx_FNS(name, sh4_scifa_offset, sh4_scifa_size, sh4_scifb_offset, sh4_scifb_size)
#define SCIF_FNS(name, scif_offset, scif_size) \
  CPU_SCIF_FNS(name, scif_offset, scif_size)
#else
#define SCIx_FNS(name, sh3_sci_offset, sh3_sci_size, sh4_sci_offset, sh4_sci_size, \
		 sh3_scif_offset, sh3_scif_size, sh4_scif_offset, sh4_scif_size, \
                 h8_sci_offset, h8_sci_size) \
  CPU_SCIx_FNS(name, sh3_sci_offset, sh3_sci_size, sh3_scif_offset, sh3_scif_size)
#define SCIF_FNS(name, sh3_scif_offset, sh3_scif_size, sh4_scif_offset, sh4_scif_size) \
  CPU_SCIF_FNS(name, sh3_scif_offset, sh3_scif_size)
#endif
#elif defined(__H8300H__) || defined(__H8300S__)
#define SCIx_FNS(name, sh3_sci_offset, sh3_sci_size, sh4_sci_offset, sh4_sci_size, \
		 sh3_scif_offset, sh3_scif_size, sh4_scif_offset, sh4_scif_size, \
                 h8_sci_offset, h8_sci_size) \
  CPU_SCI_FNS(name, h8_sci_offset, h8_sci_size)
#define SCIF_FNS(name, sh3_scif_offset, sh3_scif_size, sh4_scif_offset, sh4_scif_size) \
  CPU_SCIF_FNS(name)
#elif defined(CONFIG_CPU_SUBTYPE_SH7723) ||\
      defined(CONFIG_CPU_SUBTYPE_SH7724)
        #define SCIx_FNS(name, sh4_scifa_offset, sh4_scifa_size, sh4_scif_offset, sh4_scif_size) \
                CPU_SCIx_FNS(name, sh4_scifa_offset, sh4_scifa_size, sh4_scif_offset, sh4_scif_size)
        #define SCIF_FNS(name, sh4_scif_offset, sh4_scif_size) \
                CPU_SCIF_FNS(name, sh4_scif_offset, sh4_scif_size)
#else
#define SCIx_FNS(name, sh3_sci_offset, sh3_sci_size, sh4_sci_offset, sh4_sci_size, \
		 sh3_scif_offset, sh3_scif_size, sh4_scif_offset, sh4_scif_size, \
		 h8_sci_offset, h8_sci_size) \
  CPU_SCIx_FNS(name, sh4_sci_offset, sh4_sci_size, sh4_scif_offset, sh4_scif_size)
#define SCIF_FNS(name, sh3_scif_offset, sh3_scif_size, sh4_scif_offset, sh4_scif_size) \
  CPU_SCIF_FNS(name, sh4_scif_offset, sh4_scif_size)
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7705) || \
    defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721) || \
    defined(CONFIG_ARCH_SH73A0) || \
    defined(CONFIG_ARCH_SH7367) || \
    defined(CONFIG_ARCH_SH7377)

SCIF_FNS(SCSMR,  0x00, 16)
SCIF_FNS(SCBRR,  0x04,  8)
SCIF_FNS(SCSCR,  0x08, 16)
SCIF_FNS(SCTDSR, 0x0c,  8)
SCIF_FNS(SCFER,  0x10, 16)
SCIF_FNS(SCxSR,  0x14, 16)
SCIF_FNS(SCFCR,  0x18, 16)
SCIF_FNS(SCFDR,  0x1c, 16)
SCIF_FNS(SCxTDR, 0x20,  8)
SCIF_FNS(SCxRDR, 0x24,  8)
SCIF_FNS(SCLSR,  0x00,  0)
#elif defined(CONFIG_ARCH_SH7372)
SCIF_FNS(SCSMR,  0x00, 16)
SCIF_FNS(SCBRR,  0x04,  8)
SCIF_FNS(SCSCR,  0x08, 16)
SCIF_FNS(SCTDSR, 0x0c, 16)
SCIF_FNS(SCFER,  0x10, 16)
SCIF_FNS(SCxSR,  0x14, 16)
SCIF_FNS(SCFCR,  0x18, 16)
SCIF_FNS(SCFDR,  0x1c, 16)
SCIF_FNS(SCTFDR, 0x38, 16)
SCIF_FNS(SCRFDR, 0x3c, 16)
SCIx_FNS(SCxTDR, 0x20,  8, 0x40,  8)
SCIx_FNS(SCxRDR, 0x24,  8, 0x60,  8)
SCIF_FNS(SCLSR,  0x00,  0)
#elif defined(CONFIG_CPU_SUBTYPE_SH7723) ||\
      defined(CONFIG_CPU_SUBTYPE_SH7724)
SCIx_FNS(SCSMR,  0x00, 16, 0x00, 16)
SCIx_FNS(SCBRR,  0x04,  8, 0x04,  8)
SCIx_FNS(SCSCR,  0x08, 16, 0x08, 16)
SCIx_FNS(SCxTDR, 0x20,  8, 0x0c,  8)
SCIx_FNS(SCxSR,  0x14, 16, 0x10, 16)
SCIx_FNS(SCxRDR, 0x24,  8, 0x14,  8)
SCIx_FNS(SCSPTR, 0,     0,    0,  0)
SCIF_FNS(SCTDSR, 0x0c,  8)
SCIF_FNS(SCFER,  0x10, 16)
SCIF_FNS(SCFCR,  0x18, 16)
SCIF_FNS(SCFDR,  0x1c, 16)
SCIF_FNS(SCLSR,  0x24, 16)
#else
/*      reg      SCI/SH3   SCI/SH4  SCIF/SH3   SCIF/SH4  SCI/H8*/
/*      name     off  sz   off  sz   off  sz   off  sz   off  sz*/
SCIx_FNS(SCSMR,  0x00,  8, 0x00,  8, 0x00,  8, 0x00, 16, 0x00,  8)
SCIx_FNS(SCBRR,  0x02,  8, 0x04,  8, 0x02,  8, 0x04,  8, 0x01,  8)
SCIx_FNS(SCSCR,  0x04,  8, 0x08,  8, 0x04,  8, 0x08, 16, 0x02,  8)
SCIx_FNS(SCxTDR, 0x06,  8, 0x0c,  8, 0x06,  8, 0x0C,  8, 0x03,  8)
SCIx_FNS(SCxSR,  0x08,  8, 0x10,  8, 0x08, 16, 0x10, 16, 0x04,  8)
SCIx_FNS(SCxRDR, 0x0a,  8, 0x14,  8, 0x0A,  8, 0x14,  8, 0x05,  8)
SCIF_FNS(SCFCR,                      0x0c,  8, 0x18, 16)
#if defined(CONFIG_CPU_SUBTYPE_SH7760) || \
    defined(CONFIG_CPU_SUBTYPE_SH7780) || \
    defined(CONFIG_CPU_SUBTYPE_SH7785) || \
    defined(CONFIG_CPU_SUBTYPE_SH7786)
SCIF_FNS(SCFDR,			     0x0e, 16, 0x1C, 16)
SCIF_FNS(SCTFDR,		     0x0e, 16, 0x1C, 16)
SCIF_FNS(SCRFDR,		     0x0e, 16, 0x20, 16)
SCIF_FNS(SCSPTR,			0,  0, 0x24, 16)
SCIF_FNS(SCLSR,				0,  0, 0x28, 16)
#elif defined(CONFIG_CPU_SUBTYPE_SH7763)
SCIF_FNS(SCFDR,				0,  0, 0x1C, 16)
SCIF_FNS(SCSPTR2,			0,  0, 0x20, 16)
SCIF_FNS(SCLSR2,			0,  0, 0x24, 16)
SCIF_FNS(SCTFDR,		     0x0e, 16, 0x1C, 16)
SCIF_FNS(SCRFDR,		     0x0e, 16, 0x20, 16)
SCIF_FNS(SCSPTR,			0,  0, 0x24, 16)
SCIF_FNS(SCLSR,				0,  0, 0x28, 16)
#else
SCIF_FNS(SCFDR,                      0x0e, 16, 0x1C, 16)
#if defined(CONFIG_CPU_SUBTYPE_SH7722)
SCIF_FNS(SCSPTR,                        0,  0, 0, 0)
#else
SCIF_FNS(SCSPTR,                        0,  0, 0x20, 16)
#endif
SCIF_FNS(SCLSR,                         0,  0, 0x24, 16)
#endif
#endif
#define sci_in(port, reg) sci_##reg##_in(port)
#define sci_out(port, reg, value) sci_##reg##_out(port, value)

/* H8/300 series SCI pins assignment */
#if defined(__H8300H__) || defined(__H8300S__)
static const struct __attribute__((packed)) {
	int port;             /* GPIO port no */
	unsigned short rx,tx; /* GPIO bit no */
} h8300_sci_pins[] = {
#if defined(CONFIG_H83007) || defined(CONFIG_H83068)
	{    /* SCI0 */
		.port = H8300_GPIO_P9,
		.rx   = H8300_GPIO_B2,
		.tx   = H8300_GPIO_B0,
	},
	{    /* SCI1 */
		.port = H8300_GPIO_P9,
		.rx   = H8300_GPIO_B3,
		.tx   = H8300_GPIO_B1,
	},
	{    /* SCI2 */
		.port = H8300_GPIO_PB,
		.rx   = H8300_GPIO_B7,
		.tx   = H8300_GPIO_B6,
	}
#elif defined(CONFIG_H8S2678)
	{    /* SCI0 */
		.port = H8300_GPIO_P3,
		.rx   = H8300_GPIO_B2,
		.tx   = H8300_GPIO_B0,
	},
	{    /* SCI1 */
		.port = H8300_GPIO_P3,
		.rx   = H8300_GPIO_B3,
		.tx   = H8300_GPIO_B1,
	},
	{    /* SCI2 */
		.port = H8300_GPIO_P5,
		.rx   = H8300_GPIO_B1,
		.tx   = H8300_GPIO_B0,
	}
#endif
};
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7708) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
static inline int sci_rxd_in(struct uart_port *port)
{
	if (port->mapbase == 0xfffffe80)
		return __raw_readb(SCPDR)&0x01 ? 1 : 0; /* SCI */
	return 1;
}
#elif defined(CONFIG_CPU_SUBTYPE_SH7750)  || \
      defined(CONFIG_CPU_SUBTYPE_SH7751)  || \
      defined(CONFIG_CPU_SUBTYPE_SH7751R) || \
      defined(CONFIG_CPU_SUBTYPE_SH7750R) || \
      defined(CONFIG_CPU_SUBTYPE_SH7750S) || \
      defined(CONFIG_CPU_SUBTYPE_SH7091)
static inline int sci_rxd_in(struct uart_port *port)
{
	if (port->mapbase == 0xffe00000)
		return __raw_readb(SCSPTR1)&0x01 ? 1 : 0; /* SCI */
	return 1;
}
#elif defined(__H8300H__) || defined(__H8300S__)
static inline int sci_rxd_in(struct uart_port *port)
{
	int ch = (port->mapbase - SMR0) >> 3;
	return (H8300_SCI_DR(ch) & h8300_sci_pins[ch].rx) ? 1 : 0;
}
#else /* default case for non-SCI processors */
static inline int sci_rxd_in(struct uart_port *port)
{
	return 1;
}
#endif

/*
 * Values for the BitRate Register (SCBRR)
 *
 * The values are actually divisors for a frequency which can
 * be internal to the SH3 (14.7456MHz) or derived from an external
 * clock source.  This driver assumes the internal clock is used;
 * to support using an external clock source, config options or
 * possibly command-line options would need to be added.
 *
 * Also, to support speeds below 2400 (why?) the lower 2 bits of
 * the SCSMR register would also need to be set to non-zero values.
 *
 * -- Greg Banks 27Feb2000
 *
 * Answer: The SCBRR register is only eight bits, and the value in
 * it gets larger with lower baud rates. At around 2400 (depending on
 * the peripherial module clock) you run out of bits. However the
 * lower two bits of SCSMR allow the module clock to be divided down,
 * scaling the value which is needed in SCBRR.
 *
 * -- Stuart Menefy - 23 May 2000
 *
 * I meant, why would anyone bother with bitrates below 2400.
 *
 * -- Greg Banks - 7Jul2000
 *
 * You "speedist"!  How will I use my 110bps ASR-33 teletype with paper
 * tape reader as a console!
 *
 * -- Mitch Davis - 15 Jul 2000
 */

#if (defined(CONFIG_CPU_SUBTYPE_SH7780)  || \
     defined(CONFIG_CPU_SUBTYPE_SH7785)  || \
     defined(CONFIG_CPU_SUBTYPE_SH7786)) && \
    !defined(CONFIG_SH_SH2007)
#define SCBRR_VALUE(bps, clk) ((clk+16*bps)/(16*bps)-1)
#elif defined(CONFIG_CPU_SUBTYPE_SH7705) || \
      defined(CONFIG_CPU_SUBTYPE_SH7720) || \
      defined(CONFIG_CPU_SUBTYPE_SH7721) || \
      defined(CONFIG_ARCH_SH73A0) || \
      defined(CONFIG_ARCH_SH7367) || \
      defined(CONFIG_ARCH_SH7377) || \
      defined(CONFIG_ARCH_SH7372)
#define SCBRR_VALUE(bps, clk) (((clk*2)+16*bps)/(32*bps)-1)
#elif defined(CONFIG_CPU_SUBTYPE_SH7723) ||\
      defined(CONFIG_CPU_SUBTYPE_SH7724)
static inline int scbrr_calc(struct uart_port *port, int bps, int clk)
{
	if (port->type == PORT_SCIF)
		return (clk+16*bps)/(32*bps)-1;
	else
		return ((clk*2)+16*bps)/(16*bps)-1;
}
#define SCBRR_VALUE(bps, clk) scbrr_calc(port, bps, clk)
#elif defined(__H8300H__) || defined(__H8300S__)
#define SCBRR_VALUE(bps, clk) (((clk*1000/32)/bps)-1)
#else /* Generic SH */
#define SCBRR_VALUE(bps, clk) ((clk+16*bps)/(32*bps)-1)
#endif
