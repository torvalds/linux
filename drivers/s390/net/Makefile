#
# S/390 network devices
#

ctcm-y += ctcm_main.o ctcm_fsms.o ctcm_mpc.o ctcm_sysfs.o ctcm_dbug.o
obj-$(CONFIG_CTCM) += ctcm.o fsm.o
obj-$(CONFIG_NETIUCV) += netiucv.o fsm.o
obj-$(CONFIG_SMSGIUCV) += smsgiucv.o
obj-$(CONFIG_SMSGIUCV_EVENT) += smsgiucv_app.o
obj-$(CONFIG_LCS) += lcs.o
obj-$(CONFIG_CLAW) += claw.o
qeth-y += qeth_core_sys.o qeth_core_main.o qeth_core_mpc.o
obj-$(CONFIG_QETH) += qeth.o
qeth_l2-y += qeth_l2_main.o
obj-$(CONFIG_QETH_L2) += qeth_l2.o
qeth_l3-y += qeth_l3_main.o qeth_l3_sys.o
obj-$(CONFIG_QETH_L3) += qeth_l3.o
