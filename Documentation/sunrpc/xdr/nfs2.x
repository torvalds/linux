/*
 * This document was extracted by hand from RFC 1094. Several errors
 * and omissions were corrected. Additional sources for this protocol
 * specification include:
 *
 *   https://pubs.opengroup.org/onlinepubs/9629799/chap7.htm
 *
 * Note that RFC 1094's official date of publication (March 1989) is
 * before the IETF required its RFCs to carry an explicit copyright
 * or other IP ownership notice.
 */

pragma header nfs2;

const NFS_MAXDATA = 8192;
const NFS_MAXPATHLEN = 1024;
const NFS_MAXNAMLEN = 255;
const NFS_COOKIESIZE = 4;
const NFS_FHSIZE = 32;

/*
 * RFC 1094 calls the following enum "stat". That name conflicts with
 * the Linux kernel's "struct stat" because in C, struct, union, and
 * enum tags all share a single tag namespace. Details are specified
 * in the C11 standard, Section 6.2.3.
 */
enum nfsstat {
	NFS_OK			= 0,
	NFSERR_PERM		= 1,
	NFSERR_NOENT		= 2,
	NFSERR_IO		= 5,
	NFSERR_NXIO		= 6,
	NFSERR_ACCES		= 13,
	NFSERR_EXIST		= 17,
	NFSERR_NODEV		= 19,
	NFSERR_NOTDIR		= 20,
	NFSERR_ISDIR		= 21,
	NFSERR_FBIG		= 27,
	NFSERR_NOSPC		= 28,
	NFSERR_ROFS		= 30,
	NFSERR_NAMETOOLONG	= 63,
	NFSERR_NOTEMPTY		= 66,
	NFSERR_DQUOT		= 69,
	NFSERR_STALE		= 70,
	NFSERR_WFLUSH		= 99
};
pragma big_endian nfsstat;

enum ftype {
	NFNON		= 0,
	NFREG		= 1,
	NFDIR		= 2,
	NFBLK		= 3,
	NFCHR		= 4,
	NFLNK		= 5
};

typedef opaque		fhandle[NFS_FHSIZE];
typedef opaque		nfscookie[NFS_COOKIESIZE];

struct timeval {
	unsigned int	seconds;
	unsigned int	useconds;
};

struct fattr {
	ftype		type;
	unsigned int	mode;
	unsigned int	nlink;
	unsigned int	uid;
	unsigned int	gid;
	unsigned int	size;
	unsigned int	blocksize;
	unsigned int	rdev;
	unsigned int	blocks;
	unsigned int	fsid;
	unsigned int	fileid;
	timeval		atime;
	timeval		mtime;
	timeval		ctime;
};

struct sattr {
	unsigned int	mode;
	unsigned int	uid;
	unsigned int	gid;
	unsigned int	size;
	timeval		atime;
	timeval		mtime;
};

typedef string		filename<NFS_MAXNAMLEN>;
typedef string		path<NFS_MAXPATHLEN>;

union attrstat switch (nfsstat status) {
	case NFS_OK:
		fattr		attributes;
	default:
		void;
};

struct diropargs {
	fhandle		dir;
	filename	name;
};

struct diropok {
	fhandle		file;
	fattr		attributes;
};

union diropres switch (nfsstat status) {
	case NFS_OK:
		diropok		diropok;
	default:
		void;
};

struct sattrargs {
	fhandle		file;
	sattr		attributes;
};

union readlinkres switch (nfsstat status) {
	case NFS_OK:
		path		data;
	default:
		void;
};
pragma pages readlinkres data;

struct readargs {
	fhandle		file;
	unsigned int	offset;
	unsigned int	count;
	unsigned int	totalcount;
};

struct readresok {
	fattr		attributes;
	opaque		data<NFS_MAXDATA>;
};
pragma pages readresok data;

union readres switch (nfsstat status) {
	case NFS_OK:
		readresok	readresok;
	default:
		void;
};

struct writeargs {
	fhandle		file;
	unsigned int	beginoffset;
	unsigned int	offset;
	unsigned int	totalcount;
	opaque		data<NFS_MAXDATA>;
};
pragma pages writeargs data;

struct createargs {
	diropargs	where;
	sattr		attributes;
};

struct renameargs {
	diropargs	from;
	diropargs	to;
};

struct linkargs {
	fhandle		from;
	diropargs	to;
};

struct symlinkargs {
	diropargs	from;
	path		to;
	sattr		attributes;
};
pragma pages symlinkargs to;

struct readdirargs {
	fhandle		dir;
	nfscookie	cookie;
	unsigned int	count;
};

struct entry {
	unsigned int	fileid;
	filename	name;
	nfscookie	cookie;
	entry		*nextentry;
};

struct readdirok {
	entry		*entries;
	bool		eof;
};

/*
 * A spike encoding the entry list through the aggregate codec.  The
 * list encodes as the value-follows form -- each entry prefixed by
 * TRUE, the sequence closed by FALSE -- and the encode hooks stream it
 * straight from the directory during reply encoding rather than from a
 * materialized array.  The struct definitions above are the verbatim
 * RFC 1094 types; only this pragma selects the hook-driven codec.
 */
pragma aggregate readdirok entries;

union readdirres switch (nfsstat status) {
	case NFS_OK:
		readdirok	readdirok;
	default:
		void;
};

struct info {
	unsigned int	tsize;
	unsigned int	bsize;
	unsigned int	blocks;
	unsigned int	bfree;
	unsigned int	bavail;
};

union statfsres switch (nfsstat status) {
	case NFS_OK:
		info		info;
	default:
		void;
};

program NFS_PROGRAM {
	version NFS_VERSION {
		void		NFSPROC_NULL(void) = 0;
		attrstat	NFSPROC_GETATTR(fhandle) = 1;
		attrstat	NFSPROC_SETATTR(sattrargs) = 2;
		void		NFSPROC_ROOT(void) = 3;
		diropres	NFSPROC_LOOKUP(diropargs) = 4;
		readlinkres	NFSPROC_READLINK(fhandle) = 5;
		readres		NFSPROC_READ(readargs) = 6;
		void		NFSPROC_WRITECACHE(void) = 7;
		attrstat	NFSPROC_WRITE(writeargs) = 8;
		diropres	NFSPROC_CREATE(createargs) = 9;
		nfsstat		NFSPROC_REMOVE(diropargs) = 10;
		nfsstat		NFSPROC_RENAME(renameargs) = 11;
		nfsstat		NFSPROC_LINK(linkargs) = 12;
		nfsstat		NFSPROC_SYMLINK(symlinkargs) = 13;
		diropres	NFSPROC_MKDIR(createargs) = 14;
		nfsstat		NFSPROC_RMDIR(diropargs) = 15;
		readdirres	NFSPROC_READDIR(readdirargs) = 16;
		statfsres	NFSPROC_STATFS(fhandle) = 17;
	} = 2;
} = 100003;
