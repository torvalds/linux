# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_FMC) += fmc.o

fmc-y = fmc-core.o
fmc-y += fmc-match.o
fmc-y += fmc-sdb.o
fmc-y += fru-parse.o
fmc-y += fmc-dump.o
fmc-y += fmc-debug.o

obj-$(CONFIG_FMC_FAKEDEV) += fmc-fakedev.o
obj-$(CONFIG_FMC_TRIVIAL) += fmc-trivial.o
obj-$(CONFIG_FMC_WRITE_EEPROM) += fmc-write-eeprom.o
obj-$(CONFIG_FMC_CHARDEV) += fmc-chardev.o
