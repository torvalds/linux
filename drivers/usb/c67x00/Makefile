#
# Makefile for Cypress C67X00 USB Controller
#

ccflags-$(CONFIG_USB_DEBUG)		+= -DDEBUG

obj-$(CONFIG_USB_C67X00_HCD)		+= c67x00.o

c67x00-objs := c67x00-drv.o c67x00-ll-hpi.o c67x00-hcd.o c67x00-sched.o
