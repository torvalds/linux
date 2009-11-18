#
# Makefile for the kernel serial device drivers.
#

obj-$(CONFIG_SERIAL_CORE) += serial_core.o
obj-$(CONFIG_SERIAL_21285) += 21285.o

# These Sparc drivers have to appear before others such as 8250
# which share ttySx minor node space.  Otherwise console device
# names change and other unplesantries.
obj-$(CONFIG_SERIAL_SUNCORE) += suncore.o
obj-$(CONFIG_SERIAL_SUNHV) += sunhv.o
obj-$(CONFIG_SERIAL_SUNZILOG) += sunzilog.o
obj-$(CONFIG_SERIAL_SUNSU) += sunsu.o
obj-$(CONFIG_SERIAL_SUNSAB) += sunsab.o

obj-$(CONFIG_SERIAL_8250) += 8250.o
obj-$(CONFIG_SERIAL_8250_PNP) += 8250_pnp.o
obj-$(CONFIG_SERIAL_8250_GSC) += 8250_gsc.o
obj-$(CONFIG_SERIAL_8250_PCI) += 8250_pci.o
obj-$(CONFIG_SERIAL_8250_HP300) += 8250_hp300.o
obj-$(CONFIG_SERIAL_8250_CS) += serial_cs.o
obj-$(CONFIG_SERIAL_8250_ACORN) += 8250_acorn.o
obj-$(CONFIG_SERIAL_8250_CONSOLE) += 8250_early.o
obj-$(CONFIG_SERIAL_8250_FOURPORT) += 8250_fourport.o
obj-$(CONFIG_SERIAL_8250_ACCENT) += 8250_accent.o
obj-$(CONFIG_SERIAL_8250_BOCA) += 8250_boca.o
obj-$(CONFIG_SERIAL_8250_EXAR_ST16C554) += 8250_exar_st16c554.o
obj-$(CONFIG_SERIAL_8250_HUB6) += 8250_hub6.o
obj-$(CONFIG_SERIAL_8250_MCA) += 8250_mca.o
obj-$(CONFIG_SERIAL_AMBA_PL010) += amba-pl010.o
obj-$(CONFIG_SERIAL_AMBA_PL011) += amba-pl011.o
obj-$(CONFIG_SERIAL_CLPS711X) += clps711x.o
obj-$(CONFIG_SERIAL_PXA) += pxa.o
obj-$(CONFIG_SERIAL_PNX8XXX) += pnx8xxx_uart.o
obj-$(CONFIG_SERIAL_SA1100) += sa1100.o
obj-$(CONFIG_SERIAL_BCM63XX) += bcm63xx_uart.o
obj-$(CONFIG_SERIAL_BFIN) += bfin_5xx.o
obj-$(CONFIG_SERIAL_BFIN_SPORT) += bfin_sport_uart.o
obj-$(CONFIG_SERIAL_SAMSUNG) += samsung.o
obj-$(CONFIG_SERIAL_S3C2400) += s3c2400.o
obj-$(CONFIG_SERIAL_S3C2410) += s3c2410.o
obj-$(CONFIG_SERIAL_S3C2412) += s3c2412.o
obj-$(CONFIG_SERIAL_S3C2440) += s3c2440.o
obj-$(CONFIG_SERIAL_S3C24A0) += s3c24a0.o
obj-$(CONFIG_SERIAL_S3C6400) += s3c6400.o
obj-$(CONFIG_SERIAL_S5PC100) += s3c6400.o
obj-$(CONFIG_SERIAL_MAX3100) += max3100.o
obj-$(CONFIG_SERIAL_IP22_ZILOG) += ip22zilog.o
obj-$(CONFIG_SERIAL_MUX) += mux.o
obj-$(CONFIG_SERIAL_68328) += 68328serial.o
obj-$(CONFIG_SERIAL_68360) += 68360serial.o
obj-$(CONFIG_SERIAL_MCF) += mcf.o
obj-$(CONFIG_SERIAL_PMACZILOG) += pmac_zilog.o
obj-$(CONFIG_SERIAL_LH7A40X) += serial_lh7a40x.o
obj-$(CONFIG_SERIAL_DZ) += dz.o
obj-$(CONFIG_SERIAL_ZS) += zs.o
obj-$(CONFIG_SERIAL_SH_SCI) += sh-sci.o
obj-$(CONFIG_SERIAL_SGI_L1_CONSOLE) += sn_console.o
obj-$(CONFIG_SERIAL_CPM) += cpm_uart/
obj-$(CONFIG_SERIAL_IMX) += imx.o
obj-$(CONFIG_SERIAL_MPC52xx) += mpc52xx_uart.o
obj-$(CONFIG_SERIAL_ICOM) += icom.o
obj-$(CONFIG_SERIAL_M32R_SIO) += m32r_sio.o
obj-$(CONFIG_SERIAL_MPSC) += mpsc.o
obj-$(CONFIG_SERIAL_SB1250_DUART) += sb1250-duart.o
obj-$(CONFIG_ETRAX_SERIAL) += crisv10.o
obj-$(CONFIG_SERIAL_SC26XX) += sc26xx.o
obj-$(CONFIG_SERIAL_JSM) += jsm/
obj-$(CONFIG_SERIAL_TXX9) += serial_txx9.o
obj-$(CONFIG_SERIAL_VR41XX) += vr41xx_siu.o
obj-$(CONFIG_SERIAL_SGI_IOC4) += ioc4_serial.o
obj-$(CONFIG_SERIAL_SGI_IOC3) += ioc3_serial.o
obj-$(CONFIG_SERIAL_ATMEL) += atmel_serial.o
obj-$(CONFIG_SERIAL_UARTLITE) += uartlite.o
obj-$(CONFIG_SERIAL_MSM) += msm_serial.o
obj-$(CONFIG_SERIAL_NETX) += netx-serial.o
obj-$(CONFIG_SERIAL_OF_PLATFORM) += of_serial.o
obj-$(CONFIG_SERIAL_OF_PLATFORM_NWPSERIAL) += nwpserial.o
obj-$(CONFIG_SERIAL_KS8695) += serial_ks8695.o
obj-$(CONFIG_KGDB_SERIAL_CONSOLE) += kgdboc.o
obj-$(CONFIG_SERIAL_QE) += ucc_uart.o
obj-$(CONFIG_SERIAL_TIMBERDALE)	+= timbuart.o
