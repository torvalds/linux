/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/dirhash.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include "autofs_i.h"

/* Functions for maintenance of expiry queue */

static void autofs_init_usage(struct autofs_dirhash *dh,
			      struct autofs_dir_ent *ent)
{
	list_add_tail(&ent->exp, &dh->expiry_head);
	ent->last_usage = jiffies;
}

static void autofs_delete_usage(struct autofs_dir_ent *ent)
{
	list_del(&ent->exp);
}

void autofs_update_usage(struct autofs_dirhash *dh,
			 struct autofs_dir_ent *ent)
{
	autofs_delete_usage(ent);   /* Unlink from current position */
	autofs_init_usage(dh,ent);  /* Relink at queue tail */
}

struct autofs_dir_ent *autofs_expire(struct super_block *sb,
				     struct autofs_sb_info *sbi,
				     struct vfsmount *mnt)
{
	struct autofs_dirhash *dh = &sbi->dirhash;
	struct autofs_dir_ent *ent;
	struct dentry *dentry;
	unsigned long timeout = sbi->exp_timeout;

	while (1) {
		if ( list_empty(&dh->expiry_head) || sbi->catatonic )
			return NULL;	/* No entries */
		/* We keep the list sorted by last_usage and want old stuff */
		ent = list_entry(dh->expiry_head.next, struct autofs_dir_ent, exp);
		if (jiffies - ent->last_usage < timeout)
			break;
		/* Move to end of list in case expiry isn't desirable */
		autofs_update_usage(dh, ent);

		/* Check to see that entry is expirable */
		if ( ent->ino < AUTOFS_FIRST_DIR_INO )
			return ent; /* Symlinks are always expirable */

		/* Get the dentry for the autofs subdirectory */
		dentry = ent->dentry;

		if ( !dentry ) {
			/* Should only happen in catatonic mode */
			printk("autofs: dentry == NULL but inode range is directory, entry %s\n", ent->name);
			autofs_delete_usage(ent);
			continue;
		}

		if ( !dentry->d_inode ) {
			dput(dentry);
			printk("autofs: negative dentry on expiry queue: %s\n",
			       ent->name);
			autofs_delete_usage(ent);
			continue;
		}

		/* Make sure entry is mounted and unused; note that dentry will
		   point to the mounted-on-top root. */
		if (!S_ISDIR(dentry->d_inode->i_mode)||!d_mountpoint(dentry)) {
			DPRINTK(("autofs: not expirable (not a mounted directory): %s\n", ent->name));
			continue;
		}
		mntget(mnt);
		dget(dentry);
		if (!follow_down(&mnt, &dentry)) {
			dput(dentry);
			mntput(mnt);
			DPRINTK(("autofs: not expirable (not a mounted directory): %s\n", ent->name));
			continue;
		}
		while (d_mountpoint(dentry) && follow_down(&mnt, &dentry))
			;
		dput(dentry);

		if ( may_umount(mnt) ) {
			mntput(mnt);
			DPRINTK(("autofs: signaling expire on %s\n", ent->name));
			return ent; /* Expirable! */
		}
		DPRINTK(("autofs: didn't expire due to may_umount: %s\n", ent->name));
		mntput(mnt);
	}
	return NULL;		/* No expirable entries */
}

void autofs_initialize_hash(struct autofs_dirhash *dh) {
	memset(&dh->h, 0, AUTOFS_HASH_SIZE*sizeof(struct autofs_dir_ent *));
	INIT_LIST_HEAD(&dh->expiry_head);
}

struct autofs_dir_ent *autofs_hash_lookup(const struct autofs_dirhash *dh, struct qstr *name)
{
	struct autofs_dir_ent *dhn;

	DPRINTK(("autofs_hash_lookup: hash = 0x%08x, name = ", name->hash));
	autofs_say(name->name,name->len);

	for ( dhn = dh->h[(unsigned) name->hash % AUTOFS_HASH_SIZE] ; dhn ; dhn = dhn->next ) {
		if ( name->hash == dhn->hash &&
		     name->len == dhn->len &&
		     !memcmp(name->name, dhn->name, name->len) )
			break;
	}

	return dhn;
}

void autofs_hash_insert(struct autofs_dirhash *dh, struct autofs_dir_ent *ent)
{
	struct autofs_dir_ent **dhnp;

	DPRINTK(("autofs_hash_insert: hash = 0x%08x, name = ", ent->hash));
	autofs_say(ent->name,ent->len);

	autofs_init_usage(dh,ent);
	if (ent->dentry)
		dget(ent->dentry);

	dhnp = &dh->h[(unsigned) ent->hash % AUTOFS_HASH_SIZE];
	ent->next = *dhnp;
	ent->back = dhnp;
	*dhnp = ent;
	if ( ent->next )
		ent->next->back = &(ent->next);
}

void autofs_hash_delete(struct autofs_dir_ent *ent)
{
	*(ent->back) = ent->next;
	if ( ent->next )
		ent->next->back = ent->back;

	autofs_delete_usage(ent);

	if ( ent->dentry )
		dput(ent->dentry);
	kfree(ent->name);
	kfree(ent);
}

/*
 * Used by readdir().  We must validate "ptr", so we can't simply make it
 * a pointer.  Values below 0xffff are reserved; calling with any value
 * <= 0x10000 will return the first entry found.
 *
 * "last" can be NULL or the value returned by the last search *if* we
 * want the next sequential entry.
 */
struct autofs_dir_ent *autofs_hash_enum(const struct autofs_dirhash *dh,
					off_t *ptr, struct autofs_dir_ent *last)
{
	int bucket, ecount, i;
	struct autofs_dir_ent *ent;

	bucket = (*ptr >> 16) - 1;
	ecount = *ptr & 0xffff;

	if ( bucket < 0 ) {
		bucket = ecount = 0;
	} 

	DPRINTK(("autofs_hash_enum: bucket %d, entry %d\n", bucket, ecount));

	ent = last ? last->next : NULL;

	if ( ent ) {
		ecount++;
	} else {
		while  ( bucket < AUTOFS_HASH_SIZE ) {
			ent = dh->h[bucket];
			for ( i = ecount ; ent && i ; i-- )
				ent = ent->next;
			
			if (ent) {
				ecount++; /* Point to *next* entry */
				break;
			}
			
			bucket++; ecount = 0;
		}
	}

#ifdef DEBUG
	if ( !ent )
		printk("autofs_hash_enum: nothing found\n");
	else {
		printk("autofs_hash_enum: found hash %08x, name", ent->hash);
		autofs_say(ent->name,ent->len);
	}
#endif

	*ptr = ((bucket+1) << 16) + ecount;
	return ent;
}

/* Iterate over all the ents, and remove all dentry pointers.  Used on
   entering catatonic mode, in order to make the filesystem unmountable. */
void autofs_hash_dputall(struct autofs_dirhash *dh)
{
	int i;
	struct autofs_dir_ent *ent;

	for ( i = 0 ; i < AUTOFS_HASH_SIZE ; i++ ) {
		for ( ent = dh->h[i] ; ent ; ent = ent->next ) {
			if ( ent->dentry ) {
				dput(ent->dentry);
				ent->dentry = NULL;
			}
		}
	}
}

/* Delete everything.  This is used on filesystem destruction, so we
   make no attempt to keep the pointers valid */
void autofs_hash_nuke(struct autofs_sb_info *sbi)
{
	int i;
	struct autofs_dir_ent *ent, *nent;

	for ( i = 0 ; i < AUTOFS_HASH_SIZE ; i++ ) {
		for ( ent = sbi->dirhash.h[i] ; ent ; ent = nent ) {
			nent = ent->next;
			if ( ent->dentry )
				dput(ent->dentry);
			kfree(ent->name);
			kfree(ent);
		}
	}
	shrink_dcache_sb(sbi->sb);
}
