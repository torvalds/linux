// SPDX-License-Identifier: GPL-2.0
/*
 *  ncplib_kernel.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *  Modified 1999 Wolfram Pienkoss for NLS
 *  Modified 2000 Ben Harris, University of Cambridge for NFS NS meta-info
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ncp_fs.h"

static inline void assert_server_locked(struct ncp_server *server)
{
	if (server->lock == 0) {
		ncp_dbg(1, "server not locked!\n");
	}
}

static void ncp_add_byte(struct ncp_server *server, __u8 x)
{
	assert_server_locked(server);
	*(__u8 *) (&(server->packet[server->current_size])) = x;
	server->current_size += 1;
	return;
}

static void ncp_add_word(struct ncp_server *server, __le16 x)
{
	assert_server_locked(server);
	put_unaligned(x, (__le16 *) (&(server->packet[server->current_size])));
	server->current_size += 2;
	return;
}

static void ncp_add_be16(struct ncp_server *server, __u16 x)
{
	assert_server_locked(server);
	put_unaligned(cpu_to_be16(x), (__be16 *) (&(server->packet[server->current_size])));
	server->current_size += 2;
}

static void ncp_add_dword(struct ncp_server *server, __le32 x)
{
	assert_server_locked(server);
	put_unaligned(x, (__le32 *) (&(server->packet[server->current_size])));
	server->current_size += 4;
	return;
}

static void ncp_add_be32(struct ncp_server *server, __u32 x)
{
	assert_server_locked(server);
	put_unaligned(cpu_to_be32(x), (__be32 *)(&(server->packet[server->current_size])));
	server->current_size += 4;
}

static inline void ncp_add_dword_lh(struct ncp_server *server, __u32 x) {
	ncp_add_dword(server, cpu_to_le32(x));
}

static void ncp_add_mem(struct ncp_server *server, const void *source, int size)
{
	assert_server_locked(server);
	memcpy(&(server->packet[server->current_size]), source, size);
	server->current_size += size;
	return;
}

static void ncp_add_pstring(struct ncp_server *server, const char *s)
{
	int len = strlen(s);
	assert_server_locked(server);
	if (len > 255) {
		ncp_dbg(1, "string too long: %s\n", s);
		len = 255;
	}
	ncp_add_byte(server, len);
	ncp_add_mem(server, s, len);
	return;
}

static inline void ncp_init_request(struct ncp_server *server)
{
	ncp_lock_server(server);

	server->current_size = sizeof(struct ncp_request_header);
	server->has_subfunction = 0;
}

static inline void ncp_init_request_s(struct ncp_server *server, int subfunction)
{
	ncp_lock_server(server);
	
	server->current_size = sizeof(struct ncp_request_header) + 2;
	ncp_add_byte(server, subfunction);

	server->has_subfunction = 1;
}

static inline char *
ncp_reply_data(struct ncp_server *server, int offset)
{
	return &(server->packet[sizeof(struct ncp_reply_header) + offset]);
}

static inline u8 BVAL(const void *data)
{
	return *(const u8 *)data;
}

static u8 ncp_reply_byte(struct ncp_server *server, int offset)
{
	return *(const u8 *)ncp_reply_data(server, offset);
}

static inline u16 WVAL_LH(const void *data)
{
	return get_unaligned_le16(data);
}

static u16
ncp_reply_le16(struct ncp_server *server, int offset)
{
	return get_unaligned_le16(ncp_reply_data(server, offset));
}

static u16
ncp_reply_be16(struct ncp_server *server, int offset)
{
	return get_unaligned_be16(ncp_reply_data(server, offset));
}

static inline u32 DVAL_LH(const void *data)
{
	return get_unaligned_le32(data);
}

static __le32
ncp_reply_dword(struct ncp_server *server, int offset)
{
	return get_unaligned((__le32 *)ncp_reply_data(server, offset));
}

static inline __u32 ncp_reply_dword_lh(struct ncp_server* server, int offset) {
	return le32_to_cpu(ncp_reply_dword(server, offset));
}

int
ncp_negotiate_buffersize(struct ncp_server *server, int size, int *target)
{
	int result;

	ncp_init_request(server);
	ncp_add_be16(server, size);

	if ((result = ncp_request(server, 33)) != 0) {
		ncp_unlock_server(server);
		return result;
	}
	*target = min_t(unsigned int, ncp_reply_be16(server, 0), size);

	ncp_unlock_server(server);
	return 0;
}


/* options: 
 *	bit 0	ipx checksum
 *	bit 1	packet signing
 */
int
ncp_negotiate_size_and_options(struct ncp_server *server, 
	int size, int options, int *ret_size, int *ret_options) {
	int result;

	/* there is minimum */
	if (size < NCP_BLOCK_SIZE) size = NCP_BLOCK_SIZE;

	ncp_init_request(server);
	ncp_add_be16(server, size);
	ncp_add_byte(server, options);
	
	if ((result = ncp_request(server, 0x61)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	/* NCP over UDP returns 0 (!!!) */
	result = ncp_reply_be16(server, 0);
	if (result >= NCP_BLOCK_SIZE)
		size = min(result, size);
	*ret_size = size;
	*ret_options = ncp_reply_byte(server, 4);

	ncp_unlock_server(server);
	return 0;
}

int ncp_get_volume_info_with_number(struct ncp_server* server,
			     int n, struct ncp_volume_info* target) {
	int result;
	int len;

	ncp_init_request_s(server, 44);
	ncp_add_byte(server, n);

	if ((result = ncp_request(server, 22)) != 0) {
		goto out;
	}
	target->total_blocks = ncp_reply_dword_lh(server, 0);
	target->free_blocks = ncp_reply_dword_lh(server, 4);
	target->purgeable_blocks = ncp_reply_dword_lh(server, 8);
	target->not_yet_purgeable_blocks = ncp_reply_dword_lh(server, 12);
	target->total_dir_entries = ncp_reply_dword_lh(server, 16);
	target->available_dir_entries = ncp_reply_dword_lh(server, 20);
	target->sectors_per_block = ncp_reply_byte(server, 28);

	memset(&(target->volume_name), 0, sizeof(target->volume_name));

	result = -EIO;
	len = ncp_reply_byte(server, 29);
	if (len > NCP_VOLNAME_LEN) {
		ncp_dbg(1, "volume name too long: %d\n", len);
		goto out;
	}
	memcpy(&(target->volume_name), ncp_reply_data(server, 30), len);
	result = 0;
out:
	ncp_unlock_server(server);
	return result;
}

int ncp_get_directory_info(struct ncp_server* server, __u8 n, 
			   struct ncp_volume_info* target) {
	int result;
	int len;

	ncp_init_request_s(server, 45);
	ncp_add_byte(server, n);

	if ((result = ncp_request(server, 22)) != 0) {
		goto out;
	}
	target->total_blocks = ncp_reply_dword_lh(server, 0);
	target->free_blocks = ncp_reply_dword_lh(server, 4);
	target->purgeable_blocks = 0;
	target->not_yet_purgeable_blocks = 0;
	target->total_dir_entries = ncp_reply_dword_lh(server, 8);
	target->available_dir_entries = ncp_reply_dword_lh(server, 12);
	target->sectors_per_block = ncp_reply_byte(server, 20);

	memset(&(target->volume_name), 0, sizeof(target->volume_name));

	result = -EIO;
	len = ncp_reply_byte(server, 21);
	if (len > NCP_VOLNAME_LEN) {
		ncp_dbg(1, "volume name too long: %d\n", len);
		goto out;
	}
	memcpy(&(target->volume_name), ncp_reply_data(server, 22), len);
	result = 0;
out:
	ncp_unlock_server(server);
	return result;
}

int
ncp_close_file(struct ncp_server *server, const char *file_id)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);
	ncp_add_mem(server, file_id, 6);

	result = ncp_request(server, 66);
	ncp_unlock_server(server);
	return result;
}

int
ncp_make_closed(struct inode *inode)
{
	int err;

	err = 0;
	mutex_lock(&NCP_FINFO(inode)->open_mutex);
	if (atomic_read(&NCP_FINFO(inode)->opened) == 1) {
		atomic_set(&NCP_FINFO(inode)->opened, 0);
		err = ncp_close_file(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle);

		if (!err)
			ncp_vdbg("volnum=%d, dirent=%u, error=%d\n",
				 NCP_FINFO(inode)->volNumber,
				 NCP_FINFO(inode)->dirEntNum, err);
	}
	mutex_unlock(&NCP_FINFO(inode)->open_mutex);
	return err;
}

static void ncp_add_handle_path(struct ncp_server *server, __u8 vol_num,
				__le32 dir_base, int have_dir_base, 
				const char *path)
{
	ncp_add_byte(server, vol_num);
	ncp_add_dword(server, dir_base);
	if (have_dir_base != 0) {
		ncp_add_byte(server, 1);	/* dir_base */
	} else {
		ncp_add_byte(server, 0xff);	/* no handle */
	}
	if (path != NULL) {
		ncp_add_byte(server, 1);	/* 1 component */
		ncp_add_pstring(server, path);
	} else {
		ncp_add_byte(server, 0);
	}
}

int ncp_dirhandle_alloc(struct ncp_server* server, __u8 volnum, __le32 dirent,
			__u8* dirhandle) {
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 12);		/* subfunction */
	ncp_add_byte(server, NW_NS_DOS);
	ncp_add_byte(server, 0);
	ncp_add_word(server, 0);
	ncp_add_handle_path(server, volnum, dirent, 1, NULL);
	if ((result = ncp_request(server, 87)) == 0) {
		*dirhandle = ncp_reply_byte(server, 0);
	}
	ncp_unlock_server(server);
	return result;
}

int ncp_dirhandle_free(struct ncp_server* server, __u8 dirhandle) {
	int result;
	
	ncp_init_request_s(server, 20);
	ncp_add_byte(server, dirhandle);
	result = ncp_request(server, 22);
	ncp_unlock_server(server);
	return result;
}

void ncp_extract_file_info(const void *structure, struct nw_info_struct *target)
{
	const __u8 *name_len;
	const int info_struct_size = offsetof(struct nw_info_struct, nameLen);

	memcpy(target, structure, info_struct_size);
	name_len = structure + info_struct_size;
	target->nameLen = *name_len;
	memcpy(target->entryName, name_len + 1, *name_len);
	target->entryName[*name_len] = '\0';
	target->volNumber = le32_to_cpu(target->volNumber);
	return;
}

#ifdef CONFIG_NCPFS_NFS_NS
static inline void ncp_extract_nfs_info(const unsigned char *structure,
				 struct nw_nfs_info *target)
{
	target->mode = DVAL_LH(structure);
	target->rdev = DVAL_LH(structure + 8);
}
#endif

int ncp_obtain_nfs_info(struct ncp_server *server,
		        struct nw_info_struct *target)

{
	int result = 0;
#ifdef CONFIG_NCPFS_NFS_NS
	__u32 volnum = target->volNumber;

	if (ncp_is_nfs_extras(server, volnum)) {
		ncp_init_request(server);
		ncp_add_byte(server, 19);	/* subfunction */
		ncp_add_byte(server, server->name_space[volnum]);
		ncp_add_byte(server, NW_NS_NFS);
		ncp_add_byte(server, 0);
		ncp_add_byte(server, volnum);
		ncp_add_dword(server, target->dirEntNum);
		/* We must retrieve both nlinks and rdev, otherwise some server versions
		   report zeroes instead of valid data */
		ncp_add_dword_lh(server, NSIBM_NFS_MODE | NSIBM_NFS_NLINKS | NSIBM_NFS_RDEV);

		if ((result = ncp_request(server, 87)) == 0) {
			ncp_extract_nfs_info(ncp_reply_data(server, 0), &target->nfs);
			ncp_dbg(1, "(%s) mode=0%o, rdev=0x%x\n",
				target->entryName, target->nfs.mode,
				target->nfs.rdev);
		} else {
			target->nfs.mode = 0;
			target->nfs.rdev = 0;
		}
	        ncp_unlock_server(server);

	} else
#endif
	{
		target->nfs.mode = 0;
		target->nfs.rdev = 0;
	}
	return result;
}

/*
 * Returns information for a (one-component) name relative to
 * the specified directory.
 */
int ncp_obtain_info(struct ncp_server *server, struct inode *dir, const char *path,
			struct nw_info_struct *target)
{
	__u8  volnum = NCP_FINFO(dir)->volNumber;
	__le32 dirent = NCP_FINFO(dir)->dirEntNum;
	int result;

	if (target == NULL) {
		pr_err("%s: invalid call\n", __func__);
		return -EINVAL;
	}
	ncp_init_request(server);
	ncp_add_byte(server, 6);	/* subfunction */
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_byte(server, server->name_space[volnum]); /* N.B. twice ?? */
	ncp_add_word(server, cpu_to_le16(0x8006));	/* get all */
	ncp_add_dword(server, RIM_ALL);
	ncp_add_handle_path(server, volnum, dirent, 1, path);

	if ((result = ncp_request(server, 87)) != 0)
		goto out;
	ncp_extract_file_info(ncp_reply_data(server, 0), target);
	ncp_unlock_server(server);
	
	result = ncp_obtain_nfs_info(server, target);
	return result;

out:
	ncp_unlock_server(server);
	return result;
}

#ifdef CONFIG_NCPFS_NFS_NS
static int
ncp_obtain_DOS_dir_base(struct ncp_server *server,
		__u8 ns, __u8 volnum, __le32 dirent,
		const char *path, /* At most 1 component */
		__le32 *DOS_dir_base)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 6); /* subfunction */
	ncp_add_byte(server, ns);
	ncp_add_byte(server, ns);
	ncp_add_word(server, cpu_to_le16(0x8006)); /* get all */
	ncp_add_dword(server, RIM_DIRECTORY);
	ncp_add_handle_path(server, volnum, dirent, 1, path);

	if ((result = ncp_request(server, 87)) == 0)
	{
	   	if (DOS_dir_base) *DOS_dir_base=ncp_reply_dword(server, 0x34);
	}
	ncp_unlock_server(server);
	return result;
}
#endif /* CONFIG_NCPFS_NFS_NS */

static inline int
ncp_get_known_namespace(struct ncp_server *server, __u8 volume)
{
#if defined(CONFIG_NCPFS_OS2_NS) || defined(CONFIG_NCPFS_NFS_NS)
	int result;
	__u8 *namespace;
	__u16 no_namespaces;

	ncp_init_request(server);
	ncp_add_byte(server, 24);	/* Subfunction: Get Name Spaces Loaded */
	ncp_add_word(server, 0);
	ncp_add_byte(server, volume);

	if ((result = ncp_request(server, 87)) != 0) {
		ncp_unlock_server(server);
		return NW_NS_DOS; /* not result ?? */
	}

	result = NW_NS_DOS;
	no_namespaces = ncp_reply_le16(server, 0);
	namespace = ncp_reply_data(server, 2);

	while (no_namespaces > 0) {
		ncp_dbg(1, "found %d on %d\n", *namespace, volume);

#ifdef CONFIG_NCPFS_NFS_NS
		if ((*namespace == NW_NS_NFS) && !(server->m.flags&NCP_MOUNT_NO_NFS)) 
		{
			result = NW_NS_NFS;
			break;
		}
#endif	/* CONFIG_NCPFS_NFS_NS */
#ifdef CONFIG_NCPFS_OS2_NS
		if ((*namespace == NW_NS_OS2) && !(server->m.flags&NCP_MOUNT_NO_OS2))
		{
			result = NW_NS_OS2;
		}
#endif	/* CONFIG_NCPFS_OS2_NS */
		namespace += 1;
		no_namespaces -= 1;
	}
	ncp_unlock_server(server);
	return result;
#else	/* neither OS2 nor NFS - only DOS */
	return NW_NS_DOS;
#endif	/* defined(CONFIG_NCPFS_OS2_NS) || defined(CONFIG_NCPFS_NFS_NS) */
}

int
ncp_update_known_namespace(struct ncp_server *server, __u8 volume, int *ret_ns)
{
	int ns = ncp_get_known_namespace(server, volume);

	if (ret_ns)
		*ret_ns = ns;

	ncp_dbg(1, "namespace[%d] = %d\n", volume, server->name_space[volume]);

	if (server->name_space[volume] == ns)
		return 0;
	server->name_space[volume] = ns;
	return 1;
}

static int
ncp_ObtainSpecificDirBase(struct ncp_server *server,
		__u8 nsSrc, __u8 nsDst, __u8 vol_num, __le32 dir_base,
		const char *path, /* At most 1 component */
		__le32 *dirEntNum, __le32 *DosDirNum)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 6); /* subfunction */
	ncp_add_byte(server, nsSrc);
	ncp_add_byte(server, nsDst);
	ncp_add_word(server, cpu_to_le16(0x8006)); /* get all */
	ncp_add_dword(server, RIM_ALL);
	ncp_add_handle_path(server, vol_num, dir_base, 1, path);

	if ((result = ncp_request(server, 87)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	if (dirEntNum)
		*dirEntNum = ncp_reply_dword(server, 0x30);
	if (DosDirNum)
		*DosDirNum = ncp_reply_dword(server, 0x34);
	ncp_unlock_server(server);
	return 0;
}

int
ncp_mount_subdir(struct ncp_server *server,
		 __u8 volNumber, __u8 srcNS, __le32 dirEntNum,
		 __u32* volume, __le32* newDirEnt, __le32* newDosEnt)
{
	int dstNS;
	int result;

	ncp_update_known_namespace(server, volNumber, &dstNS);
	if ((result = ncp_ObtainSpecificDirBase(server, srcNS, dstNS, volNumber, 
				      dirEntNum, NULL, newDirEnt, newDosEnt)) != 0)
	{
		return result;
	}
	*volume = volNumber;
	server->m.mounted_vol[1] = 0;
	server->m.mounted_vol[0] = 'X';
	return 0;
}

int 
ncp_get_volume_root(struct ncp_server *server,
		    const char *volname, __u32* volume, __le32* dirent, __le32* dosdirent)
{
	int result;

	ncp_dbg(1, "looking up vol %s\n", volname);

	ncp_init_request(server);
	ncp_add_byte(server, 22);	/* Subfunction: Generate dir handle */
	ncp_add_byte(server, 0);	/* DOS namespace */
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_byte(server, 0);	/* reserved */

	ncp_add_byte(server, 0);	/* faked volume number */
	ncp_add_dword(server, 0);	/* faked dir_base */
	ncp_add_byte(server, 0xff);	/* Don't have a dir_base */
	ncp_add_byte(server, 1);	/* 1 path component */
	ncp_add_pstring(server, volname);

	if ((result = ncp_request(server, 87)) != 0) {
		ncp_unlock_server(server);
		return result;
	}
	*dirent = *dosdirent = ncp_reply_dword(server, 4);
	*volume = ncp_reply_byte(server, 8);
	ncp_unlock_server(server);
	return 0;
}

int
ncp_lookup_volume(struct ncp_server *server,
		  const char *volname, struct nw_info_struct *target)
{
	int result;

	memset(target, 0, sizeof(*target));
	result = ncp_get_volume_root(server, volname,
			&target->volNumber, &target->dirEntNum, &target->DosDirNum);
	if (result) {
		return result;
	}
	ncp_update_known_namespace(server, target->volNumber, NULL);
	target->nameLen = strlen(volname);
	memcpy(target->entryName, volname, target->nameLen+1);
	target->attributes = aDIR;
	/* set dates to Jan 1, 1986  00:00 */
	target->creationTime = target->modifyTime = cpu_to_le16(0x0000);
	target->creationDate = target->modifyDate = target->lastAccessDate = cpu_to_le16(0x0C21);
	target->nfs.mode = 0;
	return 0;
}

int ncp_modify_file_or_subdir_dos_info_path(struct ncp_server *server,
					    struct inode *dir,
					    const char *path,
					    __le32 info_mask,
					    const struct nw_modify_dos_info *info)
{
	__u8  volnum = NCP_FINFO(dir)->volNumber;
	__le32 dirent = NCP_FINFO(dir)->dirEntNum;
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 7);	/* subfunction */
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_word(server, cpu_to_le16(0x8006));	/* search attribs: all */

	ncp_add_dword(server, info_mask);
	ncp_add_mem(server, info, sizeof(*info));
	ncp_add_handle_path(server, volnum, dirent, 1, path);

	result = ncp_request(server, 87);
	ncp_unlock_server(server);
	return result;
}

int ncp_modify_file_or_subdir_dos_info(struct ncp_server *server,
				       struct inode *dir,
				       __le32 info_mask,
				       const struct nw_modify_dos_info *info)
{
	return ncp_modify_file_or_subdir_dos_info_path(server, dir, NULL,
		info_mask, info);
}

#ifdef CONFIG_NCPFS_NFS_NS
int ncp_modify_nfs_info(struct ncp_server *server, __u8 volnum, __le32 dirent,
			       __u32 mode, __u32 rdev)

{
	int result = 0;

	ncp_init_request(server);
	if (server->name_space[volnum] == NW_NS_NFS) {
		ncp_add_byte(server, 25);	/* subfunction */
		ncp_add_byte(server, server->name_space[volnum]);
		ncp_add_byte(server, NW_NS_NFS);
		ncp_add_byte(server, volnum);
		ncp_add_dword(server, dirent);
		/* we must always operate on both nlinks and rdev, otherwise
		   rdev is not set */
		ncp_add_dword_lh(server, NSIBM_NFS_MODE | NSIBM_NFS_NLINKS | NSIBM_NFS_RDEV);
		ncp_add_dword_lh(server, mode);
		ncp_add_dword_lh(server, 1);	/* nlinks */
		ncp_add_dword_lh(server, rdev);
		result = ncp_request(server, 87);
	}
	ncp_unlock_server(server);
	return result;
}
#endif


static int
ncp_DeleteNSEntry(struct ncp_server *server,
		  __u8 have_dir_base, __u8 volnum, __le32 dirent,
		  const char* name, __u8 ns, __le16 attr)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 8);	/* subfunction */
	ncp_add_byte(server, ns);
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_word(server, attr);	/* search attribs: all */
	ncp_add_handle_path(server, volnum, dirent, have_dir_base, name);

	result = ncp_request(server, 87);
	ncp_unlock_server(server);
	return result;
}

int
ncp_del_file_or_subdir2(struct ncp_server *server,
			struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	__u8  volnum;
	__le32 dirent;

	if (!inode) {
		return 0xFF;	/* Any error */
	}
	volnum = NCP_FINFO(inode)->volNumber;
	dirent = NCP_FINFO(inode)->DosDirNum;
	return ncp_DeleteNSEntry(server, 1, volnum, dirent, NULL, NW_NS_DOS, cpu_to_le16(0x8006));
}

int
ncp_del_file_or_subdir(struct ncp_server *server,
		       struct inode *dir, const char *name)
{
	__u8  volnum = NCP_FINFO(dir)->volNumber;
	__le32 dirent = NCP_FINFO(dir)->dirEntNum;
	int name_space;

	name_space = server->name_space[volnum];
#ifdef CONFIG_NCPFS_NFS_NS
	if (name_space == NW_NS_NFS)
 	{
 		int result;
 
		result=ncp_obtain_DOS_dir_base(server, name_space, volnum, dirent, name, &dirent);
 		if (result) return result;
		name = NULL;
		name_space = NW_NS_DOS;
 	}
#endif	/* CONFIG_NCPFS_NFS_NS */
	return ncp_DeleteNSEntry(server, 1, volnum, dirent, name, name_space, cpu_to_le16(0x8006));
}

static inline void ConvertToNWfromDWORD(__u16 v0, __u16 v1, __u8 ret[6])
{
	__le16 *dest = (__le16 *) ret;
	dest[1] = cpu_to_le16(v0);
	dest[2] = cpu_to_le16(v1);
	dest[0] = cpu_to_le16(v0 + 1);
	return;
}

/* If both dir and name are NULL, then in target there's already a
   looked-up entry that wants to be opened. */
int ncp_open_create_file_or_subdir(struct ncp_server *server,
				   struct inode *dir, const char *name,
				   int open_create_mode,
				   __le32 create_attributes,
				   __le16 desired_acc_rights,
				   struct ncp_entry_info *target)
{
	__le16 search_attribs = cpu_to_le16(0x0006);
	__u8  volnum;
	__le32 dirent;
	int result;

	volnum = NCP_FINFO(dir)->volNumber;
	dirent = NCP_FINFO(dir)->dirEntNum;

	if ((create_attributes & aDIR) != 0) {
		search_attribs |= cpu_to_le16(0x8000);
	}
	ncp_init_request(server);
	ncp_add_byte(server, 1);	/* subfunction */
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_byte(server, open_create_mode);
	ncp_add_word(server, search_attribs);
	ncp_add_dword(server, RIM_ALL);
	ncp_add_dword(server, create_attributes);
	/* The desired acc rights seem to be the inherited rights mask
	   for directories */
	ncp_add_word(server, desired_acc_rights);
	ncp_add_handle_path(server, volnum, dirent, 1, name);

	if ((result = ncp_request(server, 87)) != 0)
		goto out;
	if (!(create_attributes & aDIR))
		target->opened = 1;

	/* in target there's a new finfo to fill */
	ncp_extract_file_info(ncp_reply_data(server, 6), &(target->i));
	target->volume = target->i.volNumber;
	ConvertToNWfromDWORD(ncp_reply_le16(server, 0),
			     ncp_reply_le16(server, 2),
			     target->file_handle);
	
	ncp_unlock_server(server);

	(void)ncp_obtain_nfs_info(server, &(target->i));
	return 0;

out:
	ncp_unlock_server(server);
	return result;
}

int
ncp_initialize_search(struct ncp_server *server, struct inode *dir,
			struct nw_search_sequence *target)
{
	__u8  volnum = NCP_FINFO(dir)->volNumber;
	__le32 dirent = NCP_FINFO(dir)->dirEntNum;
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 2);	/* subfunction */
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_handle_path(server, volnum, dirent, 1, NULL);

	result = ncp_request(server, 87);
	if (result)
		goto out;
	memcpy(target, ncp_reply_data(server, 0), sizeof(*target));

out:
	ncp_unlock_server(server);
	return result;
}

int ncp_search_for_fileset(struct ncp_server *server,
			   struct nw_search_sequence *seq,
			   int* more,
			   int* cnt,
			   char* buffer,
			   size_t bufsize,
			   char** rbuf,
			   size_t* rsize)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 20);
	ncp_add_byte(server, server->name_space[seq->volNumber]);
	ncp_add_byte(server, 0);		/* datastream */
	ncp_add_word(server, cpu_to_le16(0x8006));
	ncp_add_dword(server, RIM_ALL);
	ncp_add_word(server, cpu_to_le16(32767));	/* max returned items */
	ncp_add_mem(server, seq, 9);
#ifdef CONFIG_NCPFS_NFS_NS
	if (server->name_space[seq->volNumber] == NW_NS_NFS) {
		ncp_add_byte(server, 0);	/* 0 byte pattern */
	} else 
#endif
	{
		ncp_add_byte(server, 2);	/* 2 byte pattern */
		ncp_add_byte(server, 0xff);	/* following is a wildcard */
		ncp_add_byte(server, '*');
	}
	result = ncp_request2(server, 87, buffer, bufsize);
	if (result) {
		ncp_unlock_server(server);
		return result;
	}
	if (server->ncp_reply_size < 12) {
		ncp_unlock_server(server);
		return 0xFF;
	}
	*rsize = server->ncp_reply_size - 12;
	ncp_unlock_server(server);
	buffer = buffer + sizeof(struct ncp_reply_header);
	*rbuf = buffer + 12;
	*cnt = WVAL_LH(buffer + 10);
	*more = BVAL(buffer + 9);
	memcpy(seq, buffer, 9);
	return 0;
}

static int
ncp_RenameNSEntry(struct ncp_server *server,
		  struct inode *old_dir, const char *old_name, __le16 old_type,
		  struct inode *new_dir, const char *new_name)
{
	int result = -EINVAL;

	if ((old_dir == NULL) || (old_name == NULL) ||
	    (new_dir == NULL) || (new_name == NULL))
		goto out;

	ncp_init_request(server);
	ncp_add_byte(server, 4);	/* subfunction */
	ncp_add_byte(server, server->name_space[NCP_FINFO(old_dir)->volNumber]);
	ncp_add_byte(server, 1);	/* rename flag */
	ncp_add_word(server, old_type);	/* search attributes */

	/* source Handle Path */
	ncp_add_byte(server, NCP_FINFO(old_dir)->volNumber);
	ncp_add_dword(server, NCP_FINFO(old_dir)->dirEntNum);
	ncp_add_byte(server, 1);
	ncp_add_byte(server, 1);	/* 1 source component */

	/* dest Handle Path */
	ncp_add_byte(server, NCP_FINFO(new_dir)->volNumber);
	ncp_add_dword(server, NCP_FINFO(new_dir)->dirEntNum);
	ncp_add_byte(server, 1);
	ncp_add_byte(server, 1);	/* 1 destination component */

	/* source path string */
	ncp_add_pstring(server, old_name);
	/* dest path string */
	ncp_add_pstring(server, new_name);

	result = ncp_request(server, 87);
	ncp_unlock_server(server);
out:
	return result;
}

int ncp_ren_or_mov_file_or_subdir(struct ncp_server *server,
				struct inode *old_dir, const char *old_name,
				struct inode *new_dir, const char *new_name)
{
        int result;
        __le16 old_type = cpu_to_le16(0x06);

/* If somebody can do it atomic, call me... vandrove@vc.cvut.cz */
	result = ncp_RenameNSEntry(server, old_dir, old_name, old_type,
	                                   new_dir, new_name);
        if (result == 0xFF)	/* File Not Found, try directory */
	{
		old_type = cpu_to_le16(0x16);
		result = ncp_RenameNSEntry(server, old_dir, old_name, old_type,
						   new_dir, new_name);
	}
	if (result != 0x92) return result;	/* All except NO_FILES_RENAMED */
	result = ncp_del_file_or_subdir(server, new_dir, new_name);
	if (result != 0) return -EACCES;
	result = ncp_RenameNSEntry(server, old_dir, old_name, old_type,
					   new_dir, new_name);
	return result;
}
	

/* We have to transfer to/from user space */
int
ncp_read_kernel(struct ncp_server *server, const char *file_id,
	     __u32 offset, __u16 to_read, char *target, int *bytes_read)
{
	const char *source;
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);
	ncp_add_mem(server, file_id, 6);
	ncp_add_be32(server, offset);
	ncp_add_be16(server, to_read);

	if ((result = ncp_request(server, 72)) != 0) {
		goto out;
	}
	*bytes_read = ncp_reply_be16(server, 0);
	if (*bytes_read > to_read) {
		result = -EINVAL;
		goto out;
	}
	source = ncp_reply_data(server, 2 + (offset & 1));

	memcpy(target, source, *bytes_read);
out:
	ncp_unlock_server(server);
	return result;
}

/* There is a problem... egrep and some other silly tools do:
	x = mmap(NULL, MAP_PRIVATE, PROT_READ|PROT_WRITE, <ncpfs fd>, 32768);
	read(<ncpfs fd>, x, 32768);
   Now copying read result by copy_to_user causes pagefault. This pagefault
   could not be handled because of server was locked due to read. So we have
   to use temporary buffer. So ncp_unlock_server must be done before
   copy_to_user (and for write, copy_from_user must be done before 
   ncp_init_request... same applies for send raw packet ioctl). Because of
   file is normally read in bigger chunks, caller provides kmalloced 
   (vmalloced) chunk of memory with size >= to_read...
 */
int
ncp_read_bounce(struct ncp_server *server, const char *file_id,
	 __u32 offset, __u16 to_read, struct iov_iter *to,
	 int *bytes_read, void *bounce, __u32 bufsize)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);
	ncp_add_mem(server, file_id, 6);
	ncp_add_be32(server, offset);
	ncp_add_be16(server, to_read);
	result = ncp_request2(server, 72, bounce, bufsize);
	ncp_unlock_server(server);
	if (!result) {
		int len = get_unaligned_be16((char *)bounce +
			  sizeof(struct ncp_reply_header));
		result = -EIO;
		if (len <= to_read) {
			char* source;

			source = (char*)bounce + 
			         sizeof(struct ncp_reply_header) + 2 + 
			         (offset & 1);
			*bytes_read = len;
			result = 0;
			if (copy_to_iter(source, len, to) != len)
				result = -EFAULT;
		}
	}
	return result;
}

int
ncp_write_kernel(struct ncp_server *server, const char *file_id,
		 __u32 offset, __u16 to_write,
		 const char *source, int *bytes_written)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);
	ncp_add_mem(server, file_id, 6);
	ncp_add_be32(server, offset);
	ncp_add_be16(server, to_write);
	ncp_add_mem(server, source, to_write);
	
	if ((result = ncp_request(server, 73)) == 0)
		*bytes_written = to_write;
	ncp_unlock_server(server);
	return result;
}

#ifdef CONFIG_NCPFS_IOCTL_LOCKING
int
ncp_LogPhysicalRecord(struct ncp_server *server, const char *file_id,
	  __u8 locktype, __u32 offset, __u32 length, __u16 timeout)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, locktype);
	ncp_add_mem(server, file_id, 6);
	ncp_add_be32(server, offset);
	ncp_add_be32(server, length);
	ncp_add_be16(server, timeout);

	if ((result = ncp_request(server, 0x1A)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}
	ncp_unlock_server(server);
	return 0;
}

int
ncp_ClearPhysicalRecord(struct ncp_server *server, const char *file_id,
	  __u32 offset, __u32 length)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);	/* who knows... lanalyzer says that */
	ncp_add_mem(server, file_id, 6);
	ncp_add_be32(server, offset);
	ncp_add_be32(server, length);

	if ((result = ncp_request(server, 0x1E)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}
	ncp_unlock_server(server);
	return 0;
}
#endif	/* CONFIG_NCPFS_IOCTL_LOCKING */

#ifdef CONFIG_NCPFS_NLS
/* This are the NLS conversion routines with inspirations and code parts
 * from the vfat file system and hints from Petr Vandrovec.
 */

int
ncp__io2vol(struct ncp_server *server, unsigned char *vname, unsigned int *vlen,
		const unsigned char *iname, unsigned int ilen, int cc)
{
	struct nls_table *in = server->nls_io;
	struct nls_table *out = server->nls_vol;
	unsigned char *vname_start;
	unsigned char *vname_end;
	const unsigned char *iname_end;

	iname_end = iname + ilen;
	vname_start = vname;
	vname_end = vname + *vlen - 1;

	while (iname < iname_end) {
		int chl;
		wchar_t ec;

		if (NCP_IS_FLAG(server, NCP_FLAG_UTF8)) {
			int k;
			unicode_t u;

			k = utf8_to_utf32(iname, iname_end - iname, &u);
			if (k < 0 || u > MAX_WCHAR_T)
				return -EINVAL;
			iname += k;
			ec = u;
		} else {
			if (*iname == NCP_ESC) {
				int k;

				if (iname_end - iname < 5)
					goto nospec;

				ec = 0;
				for (k = 1; k < 5; k++) {
					unsigned char nc;

					nc = iname[k] - '0';
					if (nc >= 10) {
						nc -= 'A' - '0' - 10;
						if ((nc < 10) || (nc > 15)) {
							goto nospec;
						}
					}
					ec = (ec << 4) | nc;
				}
				iname += 5;
			} else {
nospec:;			
				if ( (chl = in->char2uni(iname, iname_end - iname, &ec)) < 0)
					return chl;
				iname += chl;
			}
		}

		/* unitoupper should be here! */

		chl = out->uni2char(ec, vname, vname_end - vname);
		if (chl < 0)
			return chl;

		/* this is wrong... */
		if (cc) {
			int chi;

			for (chi = 0; chi < chl; chi++){
				vname[chi] = ncp_toupper(out, vname[chi]);
			}
		}
		vname += chl;
	}

	*vname = 0;
	*vlen = vname - vname_start;
	return 0;
}

int
ncp__vol2io(struct ncp_server *server, unsigned char *iname, unsigned int *ilen,
		const unsigned char *vname, unsigned int vlen, int cc)
{
	struct nls_table *in = server->nls_vol;
	struct nls_table *out = server->nls_io;
	const unsigned char *vname_end;
	unsigned char *iname_start;
	unsigned char *iname_end;
	unsigned char *vname_cc;
	int err;

	vname_cc = NULL;

	if (cc) {
		int i;

		/* this is wrong! */
		vname_cc = kmalloc(vlen, GFP_KERNEL);
		if (!vname_cc)
			return -ENOMEM;
		for (i = 0; i < vlen; i++)
			vname_cc[i] = ncp_tolower(in, vname[i]);
		vname = vname_cc;
	}

	iname_start = iname;
	iname_end = iname + *ilen - 1;
	vname_end = vname + vlen;

	while (vname < vname_end) {
		wchar_t ec;
		int chl;

		if ( (chl = in->char2uni(vname, vname_end - vname, &ec)) < 0) {
			err = chl;
			goto quit;
		}
		vname += chl;

		/* unitolower should be here! */

		if (NCP_IS_FLAG(server, NCP_FLAG_UTF8)) {
			int k;

			k = utf32_to_utf8(ec, iname, iname_end - iname);
			if (k < 0) {
				err = -ENAMETOOLONG;
				goto quit;
			}
			iname += k;
		} else {
			if ( (chl = out->uni2char(ec, iname, iname_end - iname)) >= 0) {
				iname += chl;
			} else {
				int k;

				if (iname_end - iname < 5) {
					err = -ENAMETOOLONG;
					goto quit;
				}
				*iname = NCP_ESC;
				for (k = 4; k > 0; k--) {
					unsigned char v;
					
					v = (ec & 0xF) + '0';
					if (v > '9') {
						v += 'A' - '9' - 1;
					}
					iname[k] = v;
					ec >>= 4;
				}
				iname += 5;
			}
		}
	}

	*iname = 0;
	*ilen = iname - iname_start;
	err = 0;
quit:;
	if (cc)
		kfree(vname_cc);
	return err;
}

#else

int
ncp__io2vol(unsigned char *vname, unsigned int *vlen,
		const unsigned char *iname, unsigned int ilen, int cc)
{
	int i;

	if (*vlen <= ilen)
		return -ENAMETOOLONG;

	if (cc)
		for (i = 0; i < ilen; i++) {
			*vname = toupper(*iname);
			vname++;
			iname++;
		}
	else {
		memmove(vname, iname, ilen);
		vname += ilen;
	}

	*vlen = ilen;
	*vname = 0;
	return 0;
}

int
ncp__vol2io(unsigned char *iname, unsigned int *ilen,
		const unsigned char *vname, unsigned int vlen, int cc)
{
	int i;

	if (*ilen <= vlen)
		return -ENAMETOOLONG;

	if (cc)
		for (i = 0; i < vlen; i++) {
			*iname = tolower(*vname);
			iname++;
			vname++;
		}
	else {
		memmove(iname, vname, vlen);
		iname += vlen;
	}

	*ilen = vlen;
	*iname = 0;
	return 0;
}

#endif
