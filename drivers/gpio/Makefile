# generic gpio support: dedicated expander chips, etc
#
# NOTE: platform-specific GPIO drivers don't belong in the
# drivers/gpio directory; put them with other platform setup
# code, IRQ controllers, board init, etc.

ccflags-$(CONFIG_DEBUG_GPIO)	+= -DDEBUG

obj-$(CONFIG_GPIOLIB)		+= gpiolib.o

obj-$(CONFIG_GPIO_ADP5520)	+= adp5520-gpio.o
obj-$(CONFIG_GPIO_ADP5588)	+= adp5588-gpio.o
obj-$(CONFIG_GPIO_LANGWELL)	+= langwell_gpio.o
obj-$(CONFIG_GPIO_MAX730X)	+= max730x.o
obj-$(CONFIG_GPIO_MAX7300)	+= max7300.o
obj-$(CONFIG_GPIO_MAX7301)	+= max7301.o
obj-$(CONFIG_GPIO_MAX732X)	+= max732x.o
obj-$(CONFIG_GPIO_MC33880)	+= mc33880.o
obj-$(CONFIG_GPIO_MCP23S08)	+= mcp23s08.o
obj-$(CONFIG_GPIO_PCA953X)	+= pca953x.o
obj-$(CONFIG_GPIO_PCF857X)	+= pcf857x.o
obj-$(CONFIG_GPIO_PL061)	+= pl061.o
obj-$(CONFIG_GPIO_TC35892)	+= tc35892-gpio.o
obj-$(CONFIG_GPIO_TIMBERDALE)	+= timbgpio.o
obj-$(CONFIG_GPIO_TWL4030)	+= twl4030-gpio.o
obj-$(CONFIG_GPIO_UCB1400)	+= ucb1400_gpio.o
obj-$(CONFIG_GPIO_XILINX)	+= xilinx_gpio.o
obj-$(CONFIG_GPIO_CS5535)	+= cs5535-gpio.o
obj-$(CONFIG_GPIO_BT8XX)	+= bt8xxgpio.o
obj-$(CONFIG_GPIO_IT8761E)	+= it8761e_gpio.o
obj-$(CONFIG_GPIO_VR41XX)	+= vr41xx_giu.o
obj-$(CONFIG_GPIO_WM831X)	+= wm831x-gpio.o
obj-$(CONFIG_GPIO_WM8350)	+= wm8350-gpiolib.o
obj-$(CONFIG_GPIO_WM8994)	+= wm8994-gpio.o
obj-$(CONFIG_GPIO_SCH)		+= sch_gpio.o
obj-$(CONFIG_GPIO_RDC321X)	+= rdc321x-gpio.o
obj-$(CONFIG_GPIO_JANZ_TTL)	+= janz-ttl.o
