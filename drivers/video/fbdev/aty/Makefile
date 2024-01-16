# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_FB_ATY) += atyfb.o
obj-$(CONFIG_FB_ATY128) += aty128fb.o
obj-$(CONFIG_FB_RADEON) += radeonfb.o

atyfb-y				:= atyfb_base.o mach64_accel.o mach64_cursor.o
atyfb-$(CONFIG_FB_ATY_GX)	+= mach64_gx.o
atyfb-$(CONFIG_FB_ATY_CT)	+= mach64_ct.o

atyfb-objs			:= $(atyfb-y)

radeonfb-y			:= radeon_base.o radeon_pm.o radeon_monitor.o radeon_accel.o
radeonfb-$(CONFIG_FB_RADEON_I2C)	+= radeon_i2c.o
radeonfb-$(CONFIG_FB_RADEON_BACKLIGHT)	+= radeon_backlight.o
radeonfb-objs			:= $(radeonfb-y)

