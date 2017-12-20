ifeq ($(CONFIG_MTK_COMBO), y)

ccflags-y += \
	-I$(src)/../linux/include \
	-I$(src)/../linux/pri/include \
	-I$(src)/../core/include \
	-I$(src)/../include \
	-I$(src)/../../common_detect \
	-I$(srctree)/drivers/misc/mediatek/btif/common/inc \

obj-y += wmt_core.o \
         wmt_ctrl.o \
         wmt_func.o \
         wmt_ic_soc.o \
         wmt_lib.o \
         wmt_conf.o \
         btm_core.o \
         dbg_core.o \
         psm_core.o \
         stp_core.o

endif
