#
# Makefile for nfc devices
#

obj-$(CONFIG_NFC_PN544)		+= pn544/
obj-$(CONFIG_NFC_MICROREAD)	+= microread/
obj-$(CONFIG_NFC_PN533)		+= pn533.o
obj-$(CONFIG_NFC_WILINK)	+= nfcwilink.o
obj-$(CONFIG_NFC_MEI_PHY)	+= mei_phy.o
obj-$(CONFIG_NFC_SIM)		+= nfcsim.o
obj-$(CONFIG_NFC_PORT100)	+= port100.o
obj-$(CONFIG_NFC_MRVL)		+= nfcmrvl/
obj-$(CONFIG_NFC_TRF7970A)	+= trf7970a.o
obj-$(CONFIG_NFC_ST21NFCA)  	+= st21nfca/
obj-$(CONFIG_NFC_ST21NFCB)	+= st21nfcb/
obj-$(CONFIG_NFC_NXP_NCI)	+= nxp-nci/

ccflags-$(CONFIG_NFC_DEBUG) := -DDEBUG
