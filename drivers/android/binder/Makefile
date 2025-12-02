# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(src)	# needed for trace events

obj-$(CONFIG_ANDROID_BINDER_IPC_RUST) += rust_binder.o
rust_binder-y := \
	rust_binder_main.o	\
	rust_binderfs.o		\
	rust_binder_events.o	\
	page_range_helper.o
