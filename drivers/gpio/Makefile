# gpio support: dedicated expander chips, etc

ccflags-$(CONFIG_DEBUG_GPIO)	+= -DDEBUG

obj-$(CONFIG_HAVE_GPIO_LIB)	+= gpiolib.o

obj-$(CONFIG_GPIO_MCP23S08)	+= mcp23s08.o
obj-$(CONFIG_GPIO_PCA953X)	+= pca953x.o
obj-$(CONFIG_GPIO_PCF857X)	+= pcf857x.o
