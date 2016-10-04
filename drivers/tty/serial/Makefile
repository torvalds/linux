#
# Makefile for the kernel serial device drivers.
#

obj-$(CONFIG_SERIAL_CORE) += serial_core.o
obj-$(CONFIG_SERIAL_21285) += 21285.o

obj-$(CONFIG_SERIAL_EARLYCON) += earlycon.o
obj-$(CONFIG_SERIAL_EARLYCON_ARM_SEMIHOST) += earlycon-arm-semihost.o

# These Sparc drivers have to appear before others such as 8250
# which share ttySx minor node space.  Otherwise console device
# names change and other unplesantries.
obj-$(CONFIG_SERIAL_SUNCORE) += suncore.o
obj-$(CONFIG_SERIAL_SUNHV) += sunhv.o
obj-$(CONFIG_SERIAL_SUNZILOG) += sunzilog.o
obj-$(CONFIG_SERIAL_SUNSU) += sunsu.o
obj-$(CONFIG_SERIAL_SUNSAB) += sunsab.o

# Now bring in any enabled 8250/16450/16550 type drivers.
obj-$(CONFIG_SERIAL_8250) += 8250/

obj-$(CONFIG_SERIAL_AMBA_PL010) += amba-pl010.o
obj-$(CONFIG_SERIAL_AMBA_PL011) += amba-pl011.o
obj-$(CONFIG_SERIAL_CLPS711X) += clps711x.o
obj-$(CONFIG_SERIAL_PXA_NON8250) += pxa.o
obj-$(CONFIG_SERIAL_PNX8XXX) += pnx8xxx_uart.o
obj-$(CONFIG_SERIAL_SA1100) += sa1100.o
obj-$(CONFIG_SERIAL_BCM63XX) += bcm63xx_uart.o
obj-$(CONFIG_SERIAL_BFIN) += bfin_uart.o
obj-$(CONFIG_SERIAL_BFIN_SPORT) += bfin_sport_uart.o
obj-$(CONFIG_SERIAL_SAMSUNG) += samsung.o
obj-$(CONFIG_SERIAL_MAX3100) += max3100.o
obj-$(CONFIG_SERIAL_MAX310X) += max310x.o
obj-$(CONFIG_SERIAL_IP22_ZILOG) += ip22zilog.o
obj-$(CONFIG_SERIAL_MUX) += mux.o
obj-$(CONFIG_SERIAL_MCF) += mcf.o
obj-$(CONFIG_SERIAL_PMACZILOG) += pmac_zilog.o
obj-$(CONFIG_SERIAL_HS_LPC32XX) += lpc32xx_hs.o
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
obj-$(CONFIG_SERIAL_MESON) += meson_uart.o
obj-$(CONFIG_SERIAL_SB1250_DUART) += sb1250-duart.o
obj-$(CONFIG_ETRAX_SERIAL) += crisv10.o
obj-$(CONFIG_SERIAL_ETRAXFS) += etraxfs-uart.o
obj-$(CONFIG_SERIAL_SCCNXP) += sccnxp.o
obj-$(CONFIG_SERIAL_SC16IS7XX_CORE) += sc16is7xx.o
obj-$(CONFIG_SERIAL_JSM) += jsm/
obj-$(CONFIG_SERIAL_TXX9) += serial_txx9.o
obj-$(CONFIG_SERIAL_VR41XX) += vr41xx_siu.o
obj-$(CONFIG_SERIAL_SGI_IOC4) += ioc4_serial.o
obj-$(CONFIG_SERIAL_SGI_IOC3) += ioc3_serial.o
obj-$(CONFIG_SERIAL_ATMEL) += atmel_serial.o
obj-$(CONFIG_SERIAL_UARTLITE) += uartlite.o
obj-$(CONFIG_SERIAL_MSM) += msm_serial.o
obj-$(CONFIG_SERIAL_NETX) += netx-serial.o
obj-$(CONFIG_SERIAL_KS8695) += serial_ks8695.o
obj-$(CONFIG_SERIAL_OMAP) += omap-serial.o
obj-$(CONFIG_SERIAL_ALTERA_UART) += altera_uart.o
obj-$(CONFIG_SERIAL_ST_ASC) += st-asc.o
obj-$(CONFIG_SERIAL_TILEGX) += tilegx.o
obj-$(CONFIG_SERIAL_QE) += ucc_uart.o
obj-$(CONFIG_SERIAL_TIMBERDALE)	+= timbuart.o
obj-$(CONFIG_SERIAL_GRLIB_GAISLER_APBUART) += apbuart.o
obj-$(CONFIG_SERIAL_ALTERA_JTAGUART) += altera_jtaguart.o
obj-$(CONFIG_SERIAL_VT8500) += vt8500_serial.o
obj-$(CONFIG_SERIAL_IFX6X60)  	+= ifx6x60.o
obj-$(CONFIG_SERIAL_PCH_UART)	+= pch_uart.o
obj-$(CONFIG_SERIAL_MXS_AUART) += mxs-auart.o
obj-$(CONFIG_SERIAL_LANTIQ)	+= lantiq.o
obj-$(CONFIG_SERIAL_XILINX_PS_UART) += xilinx_uartps.o
obj-$(CONFIG_SERIAL_SIRFSOC) += sirfsoc_uart.o
obj-$(CONFIG_SERIAL_TEGRA) += serial-tegra.o
obj-$(CONFIG_SERIAL_AR933X)   += ar933x_uart.o
obj-$(CONFIG_SERIAL_EFM32_UART) += efm32-uart.o
obj-$(CONFIG_SERIAL_ARC)	+= arc_uart.o
obj-$(CONFIG_SERIAL_RP2)	+= rp2.o
obj-$(CONFIG_SERIAL_FSL_LPUART)	+= fsl_lpuart.o
obj-$(CONFIG_SERIAL_CONEXANT_DIGICOLOR)	+= digicolor-usart.o
obj-$(CONFIG_SERIAL_MEN_Z135)	+= men_z135_uart.o
obj-$(CONFIG_SERIAL_SPRD) += sprd_serial.o
obj-$(CONFIG_SERIAL_STM32)	+= stm32-usart.o
obj-$(CONFIG_SERIAL_MVEBU_UART)	+= mvebu-uart.o
obj-$(CONFIG_SERIAL_PIC32)	+= pic32_uart.o
obj-$(CONFIG_SERIAL_MPS2_UART)	+= mps2-uart.o

# GPIOLIB helpers for modem control lines
obj-$(CONFIG_SERIAL_MCTRL_GPIO)	+= serial_mctrl_gpio.o

obj-$(CONFIG_SERIAL_KGDB_NMI) += kgdb_nmi.o
obj-$(CONFIG_KGDB_SERIAL_CONSOLE) += kgdboc.o
