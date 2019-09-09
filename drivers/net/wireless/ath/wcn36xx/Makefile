# SPDX-License-Identifier: ISC
obj-$(CONFIG_WCN36XX) := wcn36xx.o
wcn36xx-y +=   main.o \
               dxe.o \
               txrx.o \
               smd.o \
               pmc.o \
               debug.o

wcn36xx-$(CONFIG_NL80211_TESTMODE) += testmode.o
