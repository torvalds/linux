#
# Makefile for the kernel serial device drivers.
#
#  $Id: Makefile,v 1.8 2002/07/21 21:32:30 rmk Exp $
#

serial-8250-y :=
serial-8250-$(CONFIG_PNP) += 8250_pnp.o
serial-8250-$(CONFIG_GSC) += 8250_gsc.o
serial-8250-$(CONFIG_PCI) += 8250_pci.o
serial-8250-$(CONFIG_HP300) += 8250_hp300.o

obj-$(CONFIG_SERIAL_CORE) += serial_core.o
obj-$(CONFIG_SERIAL_21285) += 21285.o
obj-$(CONFIG_SERIAL_8250) += 8250.o $(serial-8250-y)
obj-$(CONFIG_SERIAL_8250_CS) += serial_cs.o
obj-$(CONFIG_SERIAL_8250_ACORN) += 8250_acorn.o
obj-$(CONFIG_SERIAL_8250_CONSOLE) += 8250_early.o
obj-$(CONFIG_SERIAL_8250_FOURPORT) += 8250_fourport.o
obj-$(CONFIG_SERIAL_8250_ACCENT) += 8250_accent.o
obj-$(CONFIG_SERIAL_8250_BOCA) += 8250_boca.o
obj-$(CONFIG_SERIAL_8250_HUB6) += 8250_hub6.o
obj-$(CONFIG_SERIAL_8250_MCA) += 8250_mca.o
obj-$(CONFIG_SERIAL_8250_AU1X00) += 8250_au1x00.o
obj-$(CONFIG_SERIAL_AMBA_PL010) += amba-pl010.o
obj-$(CONFIG_SERIAL_AMBA_PL011) += amba-pl011.o
obj-$(CONFIG_SERIAL_CLPS711X) += clps711x.o
obj-$(CONFIG_SERIAL_PXA) += pxa.o
obj-$(CONFIG_SERIAL_SA1100) += sa1100.o
obj-$(CONFIG_SERIAL_S3C2410) += s3c2410.o
obj-$(CONFIG_SERIAL_SUNCORE) += suncore.o
obj-$(CONFIG_SERIAL_SUNHV) += sunhv.o
obj-$(CONFIG_SERIAL_SUNZILOG) += sunzilog.o
obj-$(CONFIG_SERIAL_IP22_ZILOG) += ip22zilog.o
obj-$(CONFIG_SERIAL_SUNSU) += sunsu.o
obj-$(CONFIG_SERIAL_SUNSAB) += sunsab.o
obj-$(CONFIG_SERIAL_MUX) += mux.o
obj-$(CONFIG_SERIAL_68328) += 68328serial.o
obj-$(CONFIG_SERIAL_68360) += 68360serial.o
obj-$(CONFIG_SERIAL_COLDFIRE) += mcfserial.o
obj-$(CONFIG_V850E_UART) += v850e_uart.o
obj-$(CONFIG_SERIAL_PMACZILOG) += pmac_zilog.o
obj-$(CONFIG_SERIAL_LH7A40X) += serial_lh7a40x.o
obj-$(CONFIG_SERIAL_DZ) += dz.o
obj-$(CONFIG_SERIAL_SH_SCI) += sh-sci.o
obj-$(CONFIG_SERIAL_SGI_L1_CONSOLE) += sn_console.o
obj-$(CONFIG_SERIAL_CPM) += cpm_uart/
obj-$(CONFIG_SERIAL_IMX) += imx.o
obj-$(CONFIG_SERIAL_MPC52xx) += mpc52xx_uart.o
obj-$(CONFIG_SERIAL_ICOM) += icom.o
obj-$(CONFIG_SERIAL_M32R_SIO) += m32r_sio.o
obj-$(CONFIG_SERIAL_MPSC) += mpsc.o
obj-$(CONFIG_ETRAX_SERIAL) += crisv10.o
obj-$(CONFIG_SERIAL_JSM) += jsm/
obj-$(CONFIG_SERIAL_TXX9) += serial_txx9.o
obj-$(CONFIG_SERIAL_VR41XX) += vr41xx_siu.o
obj-$(CONFIG_SERIAL_SGI_IOC4) += ioc4_serial.o
obj-$(CONFIG_SERIAL_SGI_IOC3) += ioc3_serial.o
obj-$(CONFIG_SERIAL_AT91) += at91_serial.o
