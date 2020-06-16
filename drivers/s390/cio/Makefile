# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the S/390 common i/o drivers
#

# The following is required for define_trace.h to find ./trace.h
CFLAGS_trace.o := -I$(src)
CFLAGS_vfio_ccw_trace.o := -I$(src)

obj-y += airq.o blacklist.o chsc.o cio.o css.o chp.o idset.o isc.o \
	fcx.o itcw.o crw.o ccwreq.o trace.o ioasm.o
ccw_device-objs += device.o device_fsm.o device_ops.o
ccw_device-objs += device_id.o device_pgid.o device_status.o
obj-y += ccw_device.o cmf.o
obj-$(CONFIG_CHSC_SCH) += chsc_sch.o
obj-$(CONFIG_EADM_SCH) += eadm_sch.o
obj-$(CONFIG_SCM_BUS) += scm.o
obj-$(CONFIG_CCWGROUP) += ccwgroup.o

qdio-objs := qdio_main.o qdio_thinint.o qdio_debug.o qdio_setup.o
obj-$(CONFIG_QDIO) += qdio.o

vfio_ccw-objs += vfio_ccw_drv.o vfio_ccw_cp.o vfio_ccw_ops.o vfio_ccw_fsm.o \
	vfio_ccw_async.o vfio_ccw_trace.o vfio_ccw_chp.o
obj-$(CONFIG_VFIO_CCW) += vfio_ccw.o
