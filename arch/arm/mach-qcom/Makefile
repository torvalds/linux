obj-y			:= board.o
obj-$(CONFIG_SMP)	+= platsmp.o
obj-$(CONFIG_QCOM_SCM)	+= scm.o scm-boot.o

CFLAGS_scm.o :=$(call as-instr,.arch_extension sec,-DREQUIRES_SEC=1)
