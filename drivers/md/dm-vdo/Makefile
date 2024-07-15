# SPDX-License-Identifier: GPL-2.0-only

ccflags-y := -I$(src) -I$(src)/indexer

obj-$(CONFIG_DM_VDO) += dm-vdo.o

dm-vdo-objs := \
	action-manager.o \
	admin-state.o \
	block-map.o \
	completion.o \
	data-vio.o \
	dedupe.o \
	dm-vdo-target.o \
	dump.o \
	encodings.o \
	errors.o \
	flush.o \
	funnel-queue.o \
	funnel-workqueue.o \
	int-map.o \
	io-submitter.o \
	logger.o \
	logical-zone.o \
	memory-alloc.o \
	message-stats.o \
	murmurhash3.o \
	packer.o \
	permassert.o \
	physical-zone.o \
	priority-table.o \
	recovery-journal.o \
	repair.o \
	slab-depot.o \
	status-codes.o \
	string-utils.o \
	thread-device.o \
	thread-registry.o \
	thread-utils.o \
	vdo.o \
	vio.o \
	wait-queue.o \
	indexer/chapter-index.o \
	indexer/config.o \
	indexer/delta-index.o \
	indexer/funnel-requestqueue.o \
	indexer/geometry.o \
	indexer/index.o \
	indexer/index-layout.o \
	indexer/index-page-map.o \
	indexer/index-session.o \
	indexer/io-factory.o \
	indexer/open-chapter.o \
	indexer/radix-sort.o \
	indexer/sparse-cache.o \
	indexer/volume.o \
	indexer/volume-index.o
