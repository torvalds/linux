
obj-$(CONFIG_BCACHEFS_FS)	+= bcachefs.o

bcachefs-y		:=	\
	acl.o			\
	alloc_background.o	\
	alloc_foreground.o	\
	backpointers.o		\
	bkey.o			\
	bkey_methods.o		\
	bkey_sort.o		\
	bset.o			\
	btree_cache.o		\
	btree_gc.o		\
	btree_io.o		\
	btree_iter.o		\
	btree_journal_iter.o	\
	btree_key_cache.o	\
	btree_locking.o		\
	btree_trans_commit.o	\
	btree_update.o		\
	btree_update_interior.o	\
	btree_write_buffer.o	\
	buckets.o		\
	buckets_waiting_for_journal.o	\
	chardev.o		\
	checksum.o		\
	clock.o			\
	compress.o		\
	darray.o		\
	debug.o			\
	dirent.o		\
	disk_groups.o		\
	data_update.o		\
	ec.o			\
	errcode.o		\
	error.o			\
	extents.o		\
	extent_update.o		\
	fs.o			\
	fs-common.o		\
	fs-ioctl.o		\
	fs-io.o			\
	fs-io-buffered.o	\
	fs-io-direct.o		\
	fs-io-pagecache.o	\
	fsck.o			\
	inode.o			\
	io_read.o		\
	io_misc.o		\
	io_write.o		\
	journal.o		\
	journal_io.o		\
	journal_reclaim.o	\
	journal_sb.o		\
	journal_seq_blacklist.o	\
	keylist.o		\
	logged_ops.o		\
	lru.o			\
	mean_and_variance.o	\
	migrate.o		\
	move.o			\
	movinggc.o		\
	nocow_locking.o		\
	opts.o			\
	printbuf.o		\
	quota.o			\
	rebalance.o		\
	recovery.o		\
	reflink.o		\
	replicas.o		\
	sb-clean.o		\
	sb-counters.o		\
	sb-downgrade.o		\
	sb-errors.o		\
	sb-members.o		\
	siphash.o		\
	six.o			\
	snapshot.o		\
	subvolume.o		\
	super.o			\
	super-io.o		\
	sysfs.o			\
	tests.o			\
	time_stats.o		\
	thread_with_file.o	\
	trace.o			\
	two_state_shared_lock.o	\
	util.o			\
	varint.o		\
	xattr.o

obj-$(CONFIG_MEAN_AND_VARIANCE_UNIT_TEST)   += mean_and_variance_test.o

# Silence "note: xyz changed in GCC X.X" messages
subdir-ccflags-y += $(call cc-disable-warning, psabi)
