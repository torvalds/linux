# arch/arm/mach-s3c64xx/Makefile
#
# Copyright 2008 Openmoko, Inc.
# Copyright 2008 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# Core support for S3C6400 system

obj-$(CONFIG_CPU_S3C6400)	+= s3c6400.o
obj-$(CONFIG_CPU_S3C6410)	+= s3c6410.o

# setup support

obj-$(CONFIG_S3C64XX_SETUP_SDHCI) += setup-sdhci.o

# Machine support

obj-$(CONFIG_MACH_ANW6410)	+= mach-anw6410.o
obj-$(CONFIG_MACH_SMDK6400)	+= mach-smdk6400.o
obj-$(CONFIG_MACH_SMDK6410)	+= mach-smdk6410.o
obj-$(CONFIG_MACH_NCP)		+= mach-ncp.o
obj-$(CONFIG_MACH_HMT)		+= mach-hmt.o
