# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for GenieZone driver, this file should be include in arch's
# to avoid two ko being generated.
#

GZVM_DIR ?= ../../../drivers/virt/geniezone

gzvm-y := $(GZVM_DIR)/gzvm_main.o $(GZVM_DIR)/gzvm_vm.o \
	  $(GZVM_DIR)/gzvm_vcpu.o $(GZVM_DIR)/gzvm_irqfd.o \
	  $(GZVM_DIR)/gzvm_ioeventfd.o $(GZVM_DIR)/gzvm_mmu.o \
	  $(GZVM_DIR)/gzvm_exception.o
