========
Pangolin
========

Pangolin is a StrongARM 1110-based evaluation platform produced
by Dialogue Technology (http://www.dialogue.com.tw/).
It has EISA slots for ease of configuration with SDRAM/Flash
memory card, USB/Serial/Audio card, Compact Flash card,
PCMCIA/IDE card and TFT-LCD card.

To compile for Pangolin, you must issue the following commands::

	make pangolin_config
	make oldconfig
	make zImage

Supported peripherals
=====================

- SA1110 serial port (UART1/UART2/UART3)
- flash memory access
- compact flash driver
- UDA1341 sound driver
- SA1100 LCD controller for 800x600 16bpp TFT-LCD
- MQ-200 driver for 800x600 16bpp TFT-LCD
- Penmount(touch panel) driver
- PCMCIA driver
- SMC91C94 LAN driver
- IDE driver (experimental)
