qcom-iris-objs += \
	     iris_buffer.o \
             iris_core.o \
             iris_ctrls.o \
             iris_firmware.o \
             iris_hfi_common.o \
             iris_hfi_gen1_command.o \
             iris_hfi_gen1_response.o \
             iris_hfi_gen2_command.o \
             iris_hfi_gen2_packet.o \
             iris_hfi_gen2_response.o \
             iris_hfi_queue.o \
             iris_platform_sm8550.o \
             iris_power.o \
             iris_probe.o \
             iris_resources.o \
             iris_state.o \
             iris_utils.o \
             iris_vidc.o \
             iris_vb2.o \
             iris_vdec.o \
             iris_vpu2.o \
             iris_vpu3.o \
             iris_vpu_buffer.o \
             iris_vpu_common.o \

ifeq ($(CONFIG_VIDEO_QCOM_VENUS),)
qcom-iris-objs += iris_platform_sm8250.o
endif

obj-$(CONFIG_VIDEO_QCOM_IRIS) += qcom-iris.o
