#
# S/390 network devices
#

ctcm-y += ctcm_main.o ctcm_fsms.o ctcm_mpc.o ctcm_sysfs.o ctcm_dbug.o
obj-$(CONFIG_CTCM) += ctcm.o fsm.o cu3088.o
obj-$(CONFIG_NETIUCV) += netiucv.o fsm.o
obj-$(CONFIG_SMSGIUCV) += smsgiucv.o
obj-$(CONFIG_LCS) += lcs.o cu3088.o
obj-$(CONFIG_CLAW) += claw.o cu3088.o
qeth-y := qeth_main.o qeth_mpc.o qeth_sys.o qeth_eddp.o 
qeth-$(CONFIG_PROC_FS) += qeth_proc.o
obj-$(CONFIG_QETH) += qeth.o
