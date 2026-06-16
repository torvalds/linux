/*
 * Copyright (c) 2010 IETF Trust and the persons identified
 * as the document authors.  All rights reserved.
 *
 * The document authors are identified in RFC 3530 and
 * RFC 5661.
 *
 * Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the
 * following conditions are met:
 *
 * - Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer in the documentation and/or other
 *   materials provided with the distribution.
 *
 * - Neither the name of Internet Society, IETF or IETF
 *   Trust, nor the names of specific contributors, may be
 *   used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS
 *   AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 *   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 *   EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *   NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *   IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 *   ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

pragma header nfs4;

/*
 * Basic typedefs for RFC 1832 data type definitions
 */
typedef int                  int32_t;
typedef unsigned int         uint32_t;
typedef hyper                int64_t;
typedef unsigned hyper       uint64_t;

const NFS4_VERIFIER_SIZE        = 8;
const NFS4_FHSIZE               = 128;

enum nfsstat4 {
 NFS4_OK                = 0,    /* everything is okay      */
 NFS4ERR_PERM           = 1,    /* caller not privileged   */
 NFS4ERR_NOENT          = 2,    /* no such file/directory  */
 NFS4ERR_IO             = 5,    /* hard I/O error          */
 NFS4ERR_NXIO           = 6,    /* no such device          */
 NFS4ERR_ACCESS         = 13,   /* access denied           */
 NFS4ERR_EXIST          = 17,   /* file already exists     */
 NFS4ERR_XDEV           = 18,   /* different filesystems   */

 /*
  * Please do not allocate value 19; it was used in NFSv3
  * and we do not want a value in NFSv3 to have a different
  * meaning in NFSv4.x.
  */

 NFS4ERR_NOTDIR         = 20,   /* should be a directory   */
 NFS4ERR_ISDIR          = 21,   /* should not be directory */
 NFS4ERR_INVAL          = 22,   /* invalid argument        */
 NFS4ERR_FBIG           = 27,   /* file exceeds server max */
 NFS4ERR_NOSPC          = 28,   /* no space on filesystem  */
 NFS4ERR_ROFS           = 30,   /* read-only filesystem    */
 NFS4ERR_MLINK          = 31,   /* too many hard links     */
 NFS4ERR_NAMETOOLONG    = 63,   /* name exceeds server max */
 NFS4ERR_NOTEMPTY       = 66,   /* directory not empty     */
 NFS4ERR_DQUOT          = 69,   /* hard quota limit reached*/
 NFS4ERR_STALE          = 70,   /* file no longer exists   */
 NFS4ERR_BADHANDLE      = 10001,/* Illegal filehandle      */
 NFS4ERR_BAD_COOKIE     = 10003,/* READDIR cookie is stale */
 NFS4ERR_NOTSUPP        = 10004,/* operation not supported */
 NFS4ERR_TOOSMALL       = 10005,/* response limit exceeded */
 NFS4ERR_SERVERFAULT    = 10006,/* undefined server error  */
 NFS4ERR_BADTYPE        = 10007,/* type invalid for CREATE */
 NFS4ERR_DELAY          = 10008,/* file "busy" - retry     */
 NFS4ERR_SAME           = 10009,/* nverify says attrs same */
 NFS4ERR_DENIED         = 10010,/* lock unavailable        */
 NFS4ERR_EXPIRED        = 10011,/* lock lease expired      */
 NFS4ERR_LOCKED         = 10012,/* I/O failed due to lock  */
 NFS4ERR_GRACE          = 10013,/* in grace period         */
 NFS4ERR_FHEXPIRED      = 10014,/* filehandle expired      */
 NFS4ERR_SHARE_DENIED   = 10015,/* share reserve denied    */
 NFS4ERR_WRONGSEC       = 10016,/* wrong security flavor   */
 NFS4ERR_CLID_INUSE     = 10017,/* clientid in use         */

 /* NFS4ERR_RESOURCE is not a valid error in NFSv4.1 */
 NFS4ERR_RESOURCE       = 10018,/* resource exhaustion     */

 NFS4ERR_MOVED          = 10019,/* filesystem relocated    */
 NFS4ERR_NOFILEHANDLE   = 10020,/* current FH is not set   */
 NFS4ERR_MINOR_VERS_MISMATCH= 10021,/* minor vers not supp */
 NFS4ERR_STALE_CLIENTID = 10022,/* server has rebooted     */
 NFS4ERR_STALE_STATEID  = 10023,/* server has rebooted     */
 NFS4ERR_OLD_STATEID    = 10024,/* state is out of sync    */
 NFS4ERR_BAD_STATEID    = 10025,/* incorrect stateid       */
 NFS4ERR_BAD_SEQID      = 10026,/* request is out of seq.  */
 NFS4ERR_NOT_SAME       = 10027,/* verify - attrs not same */
 NFS4ERR_LOCK_RANGE     = 10028,/* overlapping lock range  */
 NFS4ERR_SYMLINK        = 10029,/* should be file/directory*/
 NFS4ERR_RESTOREFH      = 10030,/* no saved filehandle     */
 NFS4ERR_LEASE_MOVED    = 10031,/* some filesystem moved   */
 NFS4ERR_ATTRNOTSUPP    = 10032,/* recommended attr not sup*/
 NFS4ERR_NO_GRACE       = 10033,/* reclaim outside of grace*/
 NFS4ERR_RECLAIM_BAD    = 10034,/* reclaim error at server */
 NFS4ERR_RECLAIM_CONFLICT= 10035,/* conflict on reclaim    */
 NFS4ERR_BADXDR         = 10036,/* XDR decode failed       */
 NFS4ERR_LOCKS_HELD     = 10037,/* file locks held at CLOSE*/
 NFS4ERR_OPENMODE       = 10038,/* conflict in OPEN and I/O*/
 NFS4ERR_BADOWNER       = 10039,/* owner translation bad   */
 NFS4ERR_BADCHAR        = 10040,/* utf-8 char not supported*/
 NFS4ERR_BADNAME        = 10041,/* name not supported      */
 NFS4ERR_BAD_RANGE      = 10042,/* lock range not supported*/
 NFS4ERR_LOCK_NOTSUPP   = 10043,/* no atomic up/downgrade  */
 NFS4ERR_OP_ILLEGAL     = 10044,/* undefined operation     */
 NFS4ERR_DEADLOCK       = 10045,/* file locking deadlock   */
 NFS4ERR_FILE_OPEN      = 10046,/* open file blocks op.    */
 NFS4ERR_ADMIN_REVOKED  = 10047,/* lockowner state revoked */
 NFS4ERR_CB_PATH_DOWN   = 10048,/* callback path down      */

 /* NFSv4.1 errors start here. */

 NFS4ERR_BADIOMODE      = 10049,
 NFS4ERR_BADLAYOUT      = 10050,
 NFS4ERR_BAD_SESSION_DIGEST = 10051,
 NFS4ERR_BADSESSION     = 10052,
 NFS4ERR_BADSLOT        = 10053,
 NFS4ERR_COMPLETE_ALREADY = 10054,
 NFS4ERR_CONN_NOT_BOUND_TO_SESSION = 10055,
 NFS4ERR_DELEG_ALREADY_WANTED = 10056,
 NFS4ERR_BACK_CHAN_BUSY = 10057,/*backchan reqs outstanding*/
 NFS4ERR_LAYOUTTRYLATER = 10058,
 NFS4ERR_LAYOUTUNAVAILABLE = 10059,
 NFS4ERR_NOMATCHING_LAYOUT = 10060,
 NFS4ERR_RECALLCONFLICT = 10061,
 NFS4ERR_UNKNOWN_LAYOUTTYPE = 10062,
 NFS4ERR_SEQ_MISORDERED = 10063,/* unexpected seq.ID in req*/
 NFS4ERR_SEQUENCE_POS   = 10064,/* [CB_]SEQ. op not 1st op */
 NFS4ERR_REQ_TOO_BIG    = 10065,/* request too big         */
 NFS4ERR_REP_TOO_BIG    = 10066,/* reply too big           */
 NFS4ERR_REP_TOO_BIG_TO_CACHE =10067,/* rep. not all cached*/
 NFS4ERR_RETRY_UNCACHED_REP =10068,/* retry & rep. uncached*/
 NFS4ERR_UNSAFE_COMPOUND =10069,/* retry/recovery too hard */
 NFS4ERR_TOO_MANY_OPS   = 10070,/*too many ops in [CB_]COMP*/
 NFS4ERR_OP_NOT_IN_SESSION =10071,/* op needs [CB_]SEQ. op */
 NFS4ERR_HASH_ALG_UNSUPP = 10072, /* hash alg. not supp.   */
                                /* Error 10073 is unused.  */
 NFS4ERR_CLIENTID_BUSY  = 10074,/* clientid has state      */
 NFS4ERR_PNFS_IO_HOLE   = 10075,/* IO to _SPARSE file hole */
 NFS4ERR_SEQ_FALSE_RETRY= 10076,/* Retry != original req.  */
 NFS4ERR_BAD_HIGH_SLOT  = 10077,/* req has bad highest_slot*/
 NFS4ERR_DEADSESSION    = 10078,/*new req sent to dead sess*/
 NFS4ERR_ENCR_ALG_UNSUPP= 10079,/* encr alg. not supp.     */
 NFS4ERR_PNFS_NO_LAYOUT = 10080,/* I/O without a layout    */
 NFS4ERR_NOT_ONLY_OP    = 10081,/* addl ops not allowed    */
 NFS4ERR_WRONG_CRED     = 10082,/* op done by wrong cred   */
 NFS4ERR_WRONG_TYPE     = 10083,/* op on wrong type object */
 NFS4ERR_DIRDELEG_UNAVAIL=10084,/* delegation not avail.   */
 NFS4ERR_REJECT_DELEG   = 10085,/* cb rejected delegation  */
 NFS4ERR_RETURNCONFLICT = 10086,/* layout get before return*/
 NFS4ERR_DELEG_REVOKED  = 10087, /* deleg./layout revoked   */
 NFS4ERR_PARTNER_NOTSUPP = 10088,
 NFS4ERR_PARTNER_NO_AUTH = 10089,
 NFS4ERR_UNION_NOTSUPP = 10090,
 NFS4ERR_OFFLOAD_DENIED = 10091,
 NFS4ERR_WRONG_LFS = 10092,
 NFS4ERR_BADLABEL = 10093,
 NFS4ERR_OFFLOAD_NO_REQS = 10094,
 NFS4ERR_NOXATTR = 10095,
 NFS4ERR_XATTR2BIG = 10096
};

/*
 * Basic data types
 */
typedef opaque		attrlist4<>;
typedef uint32_t	bitmap4<>;
typedef opaque		verifier4[NFS4_VERIFIER_SIZE];
typedef uint64_t        nfs_cookie4;
typedef opaque		nfs_fh4<NFS4_FHSIZE>;

typedef opaque		utf8string<>;
typedef utf8string	utf8str_cis;
typedef utf8string	utf8str_cs;
typedef utf8string	utf8str_mixed;

typedef utf8str_cs      component4;
typedef utf8str_cs      linktext4;
typedef component4      pathname4<>;

/*
 * Timeval
 */
struct nfstime4 {
	int64_t		seconds;
	uint32_t	nseconds;
};

/*
 * File attribute container
 */
struct fattr4 {
        bitmap4         attrmask;
        attrlist4       attr_vals;
};

/*
 * Stateid
 */
struct stateid4 {
        uint32_t        seqid;
        opaque          other[12];
};

/*
 * The following content was extracted from draft-ietf-nfsv4-delstid
 */

typedef bool            fattr4_offline;


const FATTR4_OFFLINE            = 83;


struct open_arguments4 {
  bitmap4  oa_share_access;
  bitmap4  oa_share_deny;
  bitmap4  oa_share_access_want;
  bitmap4  oa_open_claim;
  bitmap4  oa_create_mode;
};


enum open_args_share_access4 {
   OPEN_ARGS_SHARE_ACCESS_READ  = 1,
   OPEN_ARGS_SHARE_ACCESS_WRITE = 2,
   OPEN_ARGS_SHARE_ACCESS_BOTH  = 3
};


enum open_args_share_deny4 {
   OPEN_ARGS_SHARE_DENY_NONE  = 0,
   OPEN_ARGS_SHARE_DENY_READ  = 1,
   OPEN_ARGS_SHARE_DENY_WRITE = 2,
   OPEN_ARGS_SHARE_DENY_BOTH  = 3
};


enum open_args_share_access_want4 {
   OPEN_ARGS_SHARE_ACCESS_WANT_ANY_DELEG           = 3,
   OPEN_ARGS_SHARE_ACCESS_WANT_NO_DELEG            = 4,
   OPEN_ARGS_SHARE_ACCESS_WANT_CANCEL              = 5,
   OPEN_ARGS_SHARE_ACCESS_WANT_SIGNAL_DELEG_WHEN_RESRC_AVAIL
                                                   = 17,
   OPEN_ARGS_SHARE_ACCESS_WANT_PUSH_DELEG_WHEN_UNCONTENDED
                                                   = 18,
   OPEN_ARGS_SHARE_ACCESS_WANT_DELEG_TIMESTAMPS    = 20,
   OPEN_ARGS_SHARE_ACCESS_WANT_OPEN_XOR_DELEGATION = 21
};


enum open_args_open_claim4 {
   OPEN_ARGS_OPEN_CLAIM_NULL          = 0,
   OPEN_ARGS_OPEN_CLAIM_PREVIOUS      = 1,
   OPEN_ARGS_OPEN_CLAIM_DELEGATE_CUR  = 2,
   OPEN_ARGS_OPEN_CLAIM_DELEGATE_PREV = 3,
   OPEN_ARGS_OPEN_CLAIM_FH            = 4,
   OPEN_ARGS_OPEN_CLAIM_DELEG_CUR_FH  = 5,
   OPEN_ARGS_OPEN_CLAIM_DELEG_PREV_FH = 6
};


enum open_args_createmode4 {
   OPEN_ARGS_CREATEMODE_UNCHECKED4     = 0,
   OPEN_ARGS_CREATE_MODE_GUARDED       = 1,
   OPEN_ARGS_CREATEMODE_EXCLUSIVE4     = 2,
   OPEN_ARGS_CREATE_MODE_EXCLUSIVE4_1  = 3
};


typedef open_arguments4 fattr4_open_arguments;
pragma public fattr4_open_arguments;


%/*
% * Determine what OPEN supports.
% */
const FATTR4_OPEN_ARGUMENTS     = 86;




const OPEN4_RESULT_NO_OPEN_STATEID = 0x00000010;


/*
 * attributes for the delegation times being
 * cached and served by the "client"
 */
typedef nfstime4        fattr4_time_deleg_access;
typedef nfstime4        fattr4_time_deleg_modify;
pragma public 		fattr4_time_deleg_access;
pragma public		fattr4_time_deleg_modify;


%/*
% * New RECOMMENDED Attribute for
% * delegation caching of times
% */
const FATTR4_TIME_DELEG_ACCESS  = 84;
const FATTR4_TIME_DELEG_MODIFY  = 85;



/* new flags for share_access field of OPEN4args */
const OPEN4_SHARE_ACCESS_WANT_DELEG_MASK        = 0xFF00;
const OPEN4_SHARE_ACCESS_WANT_NO_PREFERENCE     = 0x0000;
const OPEN4_SHARE_ACCESS_WANT_READ_DELEG        = 0x0100;
const OPEN4_SHARE_ACCESS_WANT_WRITE_DELEG       = 0x0200;
const OPEN4_SHARE_ACCESS_WANT_ANY_DELEG         = 0x0300;
const OPEN4_SHARE_ACCESS_WANT_NO_DELEG          = 0x0400;
const OPEN4_SHARE_ACCESS_WANT_CANCEL            = 0x0500;

const OPEN4_SHARE_ACCESS_WANT_SIGNAL_DELEG_WHEN_RESRC_AVAIL = 0x10000;
const OPEN4_SHARE_ACCESS_WANT_PUSH_DELEG_WHEN_UNCONTENDED = 0x20000;
const OPEN4_SHARE_ACCESS_WANT_DELEG_TIMESTAMPS = 0x100000;
const OPEN4_SHARE_ACCESS_WANT_OPEN_XOR_DELEGATION = 0x200000;

enum open_delegation_type4 {
       OPEN_DELEGATE_NONE                  = 0,
       OPEN_DELEGATE_READ                  = 1,
       OPEN_DELEGATE_WRITE                 = 2,
       OPEN_DELEGATE_NONE_EXT              = 3, /* new to v4.1 */
       OPEN_DELEGATE_READ_ATTRS_DELEG      = 4,
       OPEN_DELEGATE_WRITE_ATTRS_DELEG     = 5
};


/*
 * The following content was extracted from draft-ietf-nfsv4-posix-acls
 */

enum aclmodel4 {
	ACL_MODEL_NFS4		= 1,
	ACL_MODEL_POSIX_DRAFT	= 2,
	ACL_MODEL_NONE		= 3
};
pragma public aclmodel4;

enum aclscope4 {
	ACL_SCOPE_FILE_OBJECT	= 1,
	ACL_SCOPE_FILE_SYSTEM	= 2,
	ACL_SCOPE_SERVER	= 3
};
pragma public aclscope4;

enum posixacetag4 {
	POSIXACE4_TAG_USER_OBJ	= 1,
	POSIXACE4_TAG_USER	= 2,
	POSIXACE4_TAG_GROUP_OBJ	= 3,
	POSIXACE4_TAG_GROUP	= 4,
	POSIXACE4_TAG_MASK	= 5,
	POSIXACE4_TAG_OTHER	= 6
};
pragma public posixacetag4;

typedef uint32_t	posixaceperm4;
pragma public posixaceperm4;

/* Bit definitions for posixaceperm4. */
const POSIXACE4_PERM_EXECUTE	= 0x00000001;
const POSIXACE4_PERM_WRITE	= 0x00000002;
const POSIXACE4_PERM_READ	= 0x00000004;

struct posixace4 {
	posixacetag4		tag;
	posixaceperm4		perm;
	utf8str_mixed		who;
};

typedef aclmodel4	fattr4_acl_trueform;
typedef aclscope4	fattr4_acl_trueform_scope;
typedef posixace4	fattr4_posix_default_acl<>;
typedef posixace4	fattr4_posix_access_acl<>;

%/*
% * New for POSIX ACL extension
% */
const FATTR4_ACL_TRUEFORM	= 89;
const FATTR4_ACL_TRUEFORM_SCOPE	= 90;
const FATTR4_POSIX_DEFAULT_ACL	= 91;
const FATTR4_POSIX_ACCESS_ACL	= 92;

/*
 * Directory notification types.
 */
enum notify_type4 {
        NOTIFY4_CHANGE_CHILD_ATTRS = 0,
        NOTIFY4_CHANGE_DIR_ATTRS = 1,
        NOTIFY4_REMOVE_ENTRY = 2,
        NOTIFY4_ADD_ENTRY = 3,
        NOTIFY4_RENAME_ENTRY = 4,
        NOTIFY4_CHANGE_COOKIE_VERIFIER = 5,
        /* Proposed in RFC8881bis */
        NOTIFY4_GFLAG_EXTEND = 6,
        NOTIFY4_AUFLAG_VALID = 7,
        NOTIFY4_AUFLAG_USER = 8,
        NOTIFY4_AUFLAG_GROUP = 9,
        NOTIFY4_AUFLAG_OTHER = 10,
        NOTIFY4_CHANGE_AUTH = 11,
        NOTIFY4_CFLAG_ORDER = 12,
        NOTIFY4_AUFLAG_GANOW = 13,
        NOTIFY4_AUFLAG_GALATER = 14,
        NOTIFY4_CHANGE_GA = 15,
        NOTIFY4_CHANGE_AMASK = 16
};

/* Changed entry information.  */
struct notify_entry4 {
        component4      ne_file;
        fattr4          ne_attrs;
};

/* Previous entry information */
struct prev_entry4 {
        notify_entry4   pe_prev_entry;
        /* what READDIR returned for this entry */
        nfs_cookie4     pe_prev_entry_cookie;
};

struct notify_remove4 {
        notify_entry4   nrm_old_entry;
        nfs_cookie4     nrm_old_entry_cookie;
};

struct notify_add4 {
        /*
         * Information on object
         * possibly renamed over.
         */
        notify_remove4      nad_old_entry<1>;
        notify_entry4       nad_new_entry;
        /* what READDIR would have returned for this entry */
        nfs_cookie4         nad_new_entry_cookie<1>;
        prev_entry4         nad_prev_entry<1>;
        bool                nad_last_entry;
};

struct notify_attr4 {
        notify_entry4   na_changed_entry;
};

struct notify_rename4 {
        notify_remove4  nrn_old_entry;
        notify_add4     nrn_new_entry;
};

struct notify_verifier4 {
        verifier4       nv_old_cookieverf;
        verifier4       nv_new_cookieverf;
};

/*
 * Objects of type notify_<>4 and
 * notify_device_<>4 are encoded in this.
 */
typedef opaque notifylist4<>;

struct notify4 {
        /* composed from notify_type4 or notify_deviceid_type4 */
        bitmap4         notify_mask;
        notifylist4     notify_vals;
};

struct CB_NOTIFY4args {
        stateid4    cna_stateid;
        nfs_fh4     cna_fh;
        notify4     cna_changes<>;
};

struct CB_NOTIFY4res {
        nfsstat4    cnr_status;
};
