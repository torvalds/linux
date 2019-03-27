/*
 * authkeys.c - routines to manage the storage of authentication keys
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <math.h>
#include <stdio.h>

#include "ntp.h"
#include "ntp_fp.h"
#include "ntpd.h"
#include "ntp_lists.h"
#include "ntp_string.h"
#include "ntp_malloc.h"
#include "ntp_stdlib.h"
#include "ntp_keyacc.h"

/*
 * Structure to store keys in in the hash table.
 */
typedef struct savekey symkey;

struct savekey {
	symkey *	hlink;		/* next in hash bucket */
	DECL_DLIST_LINK(symkey, llink);	/* for overall & free lists */
	u_char *	secret;		/* shared secret */
	KeyAccT *	keyacclist;	/* Private key access list */
	u_long		lifetime;	/* remaining lifetime */
	keyid_t		keyid;		/* key identifier */
	u_short		type;		/* OpenSSL digest NID */
	size_t		secretsize;	/* secret octets */
	u_short		flags;		/* KEY_ flags that wave */
};

/* define the payload region of symkey beyond the list pointers */
#define symkey_payload	secret

#define	KEY_TRUSTED	0x001	/* this key is trusted */

#ifdef DEBUG
typedef struct symkey_alloc_tag symkey_alloc;

struct symkey_alloc_tag {
	symkey_alloc *	link;
	void *		mem;		/* enable free() atexit */
};

symkey_alloc *	authallocs;
#endif	/* DEBUG */

static u_short	auth_log2(size_t);
static void		auth_resize_hashtable(void);
static void		allocsymkey(keyid_t,	u_short,
				    u_short, u_long, size_t, u_char *, KeyAccT *);
static void		freesymkey(symkey *);
#ifdef DEBUG
static void		free_auth_mem(void);
#endif

symkey	key_listhead;		/* list of all in-use keys */;
/*
 * The hash table. This is indexed by the low order bits of the
 * keyid. We make this fairly big for potentially busy servers.
 */
#define	DEF_AUTHHASHSIZE	64
/*#define	HASHMASK	((HASHSIZE)-1)*/
#define	KEYHASH(keyid)	((keyid) & authhashmask)

int	authhashdisabled;
u_short	authhashbuckets = DEF_AUTHHASHSIZE;
u_short authhashmask = DEF_AUTHHASHSIZE - 1;
symkey **key_hash;

u_long authkeynotfound;		/* keys not found */
u_long authkeylookups;		/* calls to lookup keys */
u_long authnumkeys;		/* number of active keys */
u_long authkeyexpired;		/* key lifetime expirations */
u_long authkeyuncached;		/* cache misses */
u_long authnokey;		/* calls to encrypt with no key */
u_long authencryptions;		/* calls to encrypt */
u_long authdecryptions;		/* calls to decrypt */

/*
 * Storage for free symkey structures.  We malloc() such things but
 * never free them.
 */
symkey *authfreekeys;
int authnumfreekeys;

#define	MEMINC	16		/* number of new free ones to get */

/*
 * The key cache. We cache the last key we looked at here.
 * Note: this should hold the last *trusted* key. Also the
 * cache is only loaded when the digest type / MAC algorithm
 * is valid.
 */
keyid_t	cache_keyid;		/* key identifier */
u_char *cache_secret;		/* secret */
size_t	cache_secretsize;	/* secret length */
int	cache_type;		/* OpenSSL digest NID */
u_short cache_flags;		/* flags that wave */
KeyAccT *cache_keyacclist;	/* key access list */

/* --------------------------------------------------------------------
 * manage key access lists
 * --------------------------------------------------------------------
 */
/* allocate and populate new access node and pushes it on the list.
 * Returns the new head.
 */
KeyAccT*
keyacc_new_push(
	KeyAccT          * head,
	const sockaddr_u * addr,
	unsigned int	   subnetbits
	)
{
	KeyAccT *	node = emalloc(sizeof(KeyAccT));
	
	memcpy(&node->addr, addr, sizeof(sockaddr_u));
	node->subnetbits = subnetbits;
	node->next = head;

	return node;
}

/* ----------------------------------------------------------------- */
/* pop and deallocate the first node of a list of access nodes, if
 * the list is not empty. Returns the tail of the list.
 */
KeyAccT*
keyacc_pop_free(
	KeyAccT *head
	)
{
	KeyAccT *	next = NULL;
	if (head) {
		next = head->next;
		free(head);
	}
	return next;
}

/* ----------------------------------------------------------------- */
/* deallocate the list; returns an empty list. */
KeyAccT*
keyacc_all_free(
	KeyAccT * head
	)
{
	while (head)
		head = keyacc_pop_free(head);
	return head;
}

/* ----------------------------------------------------------------- */
/* scan a list to see if it contains a given address. Return the
 * default result value in case of an empty list.
 */
int /*BOOL*/
keyacc_contains(
	const KeyAccT    *head,
	const sockaddr_u *addr,
	int               defv)
{
	if (head) {
		do {
			if (keyacc_amatch(&head->addr, addr,
					  head->subnetbits))
				return TRUE;
		} while (NULL != (head = head->next));
		return FALSE;
	} else {
		return !!defv;
	}
}

#if CHAR_BIT != 8
# error "don't know how to handle bytes with that bit size"
#endif

/* ----------------------------------------------------------------- */
/* check two addresses for a match, taking a prefix length into account
 * when doing the compare.
 *
 * The ISC lib contains a similar function with not entirely specified
 * semantics, so it seemed somewhat cleaner to do this from scratch.
 *
 * Note 1: It *is* assumed that the addresses are stored in network byte
 * order, that is, most significant byte first!
 *
 * Note 2: "no address" compares unequal to all other addresses, even to
 * itself. This has the same semantics as NaNs have for floats: *any*
 * relational or equality operation involving a NaN returns FALSE, even
 * equality with itself. "no address" is either a NULL pointer argument
 * or an address of type AF_UNSPEC.
 */
int/*BOOL*/
keyacc_amatch(
	const sockaddr_u *	a1,
	const sockaddr_u *	a2,
	unsigned int		mbits
	)
{
	const uint8_t * pm1;
	const uint8_t * pm2;
	uint8_t         msk;
	unsigned int    len;

	/* 1st check: If any address is not an address, it's inequal. */
	if ( !a1 || (AF_UNSPEC == AF(a1)) ||
	     !a2 || (AF_UNSPEC == AF(a2))  )
		return FALSE;

	/* We could check pointers for equality here and shortcut the
	 * other checks if we find object identity. But that use case is
	 * too rare to care for it.
	 */
	
	/* 2nd check: Address families must be the same. */
	if (AF(a1) != AF(a2))
		return FALSE;

	/* type check: address family determines buffer & size */
	switch (AF(a1)) {
	case AF_INET:
		/* IPv4 is easy: clamp size, get byte pointers */
		if (mbits > sizeof(NSRCADR(a1)) * 8)
			mbits = sizeof(NSRCADR(a1)) * 8;
		pm1 = (const void*)&NSRCADR(a1);
		pm2 = (const void*)&NSRCADR(a2);
		break;

	case AF_INET6:
		/* IPv6 is slightly different: Both scopes must match,
		 * too, before we even consider doing a match!
		 */
		if ( ! SCOPE_EQ(a1, a2))
			return FALSE;
		if (mbits > sizeof(NSRCADR6(a1)) * 8)
			mbits = sizeof(NSRCADR6(a1)) * 8;
		pm1 = (const void*)&NSRCADR6(a1);
		pm2 = (const void*)&NSRCADR6(a2);
		break;

	default:
		/* don't know how to compare that!?! */
		return FALSE;
	}

	/* Split bit length into byte length and partial byte mask.
	 * Note that the byte mask extends from the MSB of a byte down,
	 * and that zero shift (--> mbits % 8 == 0) results in an
	 * all-zero mask.
	 */
	msk = 0xFFu ^ (0xFFu >> (mbits & 7));
	len = mbits >> 3;

	/* 3rd check: Do memcmp() over full bytes, if any */
	if (len && memcmp(pm1, pm2, len))
		return FALSE;

	/* 4th check: compare last incomplete byte, if any */
	if (msk && ((pm1[len] ^ pm2[len]) & msk))
		return FALSE;

	/* If none of the above failed, we're successfully through. */
	return TRUE;
}

/*
 * init_auth - initialize internal data
 */
void
init_auth(void)
{
	size_t newalloc;

	/*
	 * Initialize hash table and free list
	 */
	newalloc = authhashbuckets * sizeof(key_hash[0]);

	key_hash = erealloc(key_hash, newalloc);
	memset(key_hash, '\0', newalloc);

	INIT_DLIST(key_listhead, llink);

#ifdef DEBUG
	atexit(&free_auth_mem);
#endif
}


/*
 * free_auth_mem - assist in leak detection by freeing all dynamic
 *		   allocations from this module.
 */
#ifdef DEBUG
static void
free_auth_mem(void)
{
	symkey *	sk;
	symkey_alloc *	alloc;
	symkey_alloc *	next_alloc;

	while (NULL != (sk = HEAD_DLIST(key_listhead, llink))) {
		freesymkey(sk);
	}
	free(key_hash);
	key_hash = NULL;
	cache_keyid = 0;
	cache_flags = 0;
	cache_keyacclist = NULL;
	for (alloc = authallocs; alloc != NULL; alloc = next_alloc) {
		next_alloc = alloc->link;
		free(alloc->mem);	
	}
	authfreekeys = NULL;
	authnumfreekeys = 0;
}
#endif	/* DEBUG */


/*
 * auth_moremem - get some more free key structures
 */
void
auth_moremem(
	int	keycount
	)
{
	symkey *	sk;
	int		i;
#ifdef DEBUG
	void *		base;
	symkey_alloc *	allocrec;
# define MOREMEM_EXTRA_ALLOC	(sizeof(*allocrec))
#else
# define MOREMEM_EXTRA_ALLOC	(0)
#endif

	i = (keycount > 0)
		? keycount
		: MEMINC;
	sk = eallocarrayxz(i, sizeof(*sk), MOREMEM_EXTRA_ALLOC);
#ifdef DEBUG
	base = sk;
#endif
	authnumfreekeys += i;

	for (; i > 0; i--, sk++) {
		LINK_SLIST(authfreekeys, sk, llink.f);
	}

#ifdef DEBUG
	allocrec = (void *)sk;
	allocrec->mem = base;
	LINK_SLIST(authallocs, allocrec, link);
#endif
}


/*
 * auth_prealloc_symkeys
 */
void
auth_prealloc_symkeys(
	int	keycount
	)
{
	int	allocated;
	int	additional;

	allocated = authnumkeys + authnumfreekeys;
	additional = keycount - allocated;
	if (additional > 0)
		auth_moremem(additional);
	auth_resize_hashtable();
}


static u_short
auth_log2(size_t x)
{
	/*
	** bithack to calculate floor(log2(x))
	**
	** This assumes
	**   - (sizeof(size_t) is a power of two
	**   - CHAR_BITS is a power of two
	**   - returning zero for arguments <= 0 is OK.
	**
	** Does only shifts, masks and sums in integer arithmetic in
	** log2(CHAR_BIT*sizeof(size_t)) steps. (that is, 5/6 steps for
	** 32bit/64bit size_t)
	*/
	int	s;
	int	r = 0;
	size_t  m = ~(size_t)0;

	for (s = sizeof(size_t) / 2 * CHAR_BIT; s != 0; s >>= 1) {
		m <<= s;
		if (x & m)
			r += s;
		else
			x <<= s;
	}
	return (u_short)r;
}

int/*BOOL*/
ipaddr_match_masked(const sockaddr_u *,const sockaddr_u *,
		    unsigned int mbits);

static void
authcache_flush_id(
	keyid_t id
	)
{
	if (cache_keyid == id) {
		cache_keyid = 0;
		cache_type = 0;
		cache_flags = 0;
		cache_secret = NULL;
		cache_secretsize = 0;
		cache_keyacclist = NULL;
	}
}


/*
 * auth_resize_hashtable
 *
 * Size hash table to average 4 or fewer entries per bucket initially,
 * within the bounds of at least 4 and no more than 15 bits for the hash
 * table index.  Populate the hash table.
 */
static void
auth_resize_hashtable(void)
{
	u_long		totalkeys;
	u_short		hashbits;
	u_short		hash;
	size_t		newalloc;
	symkey *	sk;

	totalkeys = authnumkeys + authnumfreekeys;
	hashbits = auth_log2(totalkeys / 4) + 1;
	hashbits = max(4, hashbits);
	hashbits = min(15, hashbits);

	authhashbuckets = 1 << hashbits;
	authhashmask = authhashbuckets - 1;
	newalloc = authhashbuckets * sizeof(key_hash[0]);

	key_hash = erealloc(key_hash, newalloc);
	memset(key_hash, '\0', newalloc);

	ITER_DLIST_BEGIN(key_listhead, sk, llink, symkey)
		hash = KEYHASH(sk->keyid);
		LINK_SLIST(key_hash[hash], sk, hlink);
	ITER_DLIST_END()
}


/*
 * allocsymkey - common code to allocate and link in symkey
 *
 * secret must be allocated with a free-compatible allocator.  It is
 * owned by the referring symkey structure, and will be free()d by
 * freesymkey().
 */
static void
allocsymkey(
	keyid_t		id,
	u_short		flags,
	u_short		type,
	u_long		lifetime,
	size_t		secretsize,
	u_char *	secret,
	KeyAccT *	ka
	)
{
	symkey *	sk;
	symkey **	bucket;

	bucket = &key_hash[KEYHASH(id)];


	if (authnumfreekeys < 1)
		auth_moremem(-1);
	UNLINK_HEAD_SLIST(sk, authfreekeys, llink.f);
	DEBUG_ENSURE(sk != NULL);
	sk->keyid = id;
	sk->flags = flags;
	sk->type = type;
	sk->secretsize = secretsize;
	sk->secret = secret;
	sk->keyacclist = ka;
	sk->lifetime = lifetime;
	LINK_SLIST(*bucket, sk, hlink);
	LINK_TAIL_DLIST(key_listhead, sk, llink);
	authnumfreekeys--;
	authnumkeys++;
}


/*
 * freesymkey - common code to remove a symkey and recycle its entry.
 */
static void
freesymkey(
	symkey *	sk
	)
{
	symkey **	bucket;
	symkey *	unlinked;

	if (NULL == sk)
		return;

	authcache_flush_id(sk->keyid);
	keyacc_all_free(sk->keyacclist);
	
	bucket = &key_hash[KEYHASH(sk->keyid)];
	if (sk->secret != NULL) {
		memset(sk->secret, '\0', sk->secretsize);
		free(sk->secret);
	}
	UNLINK_SLIST(unlinked, *bucket, sk, hlink, symkey);
	DEBUG_ENSURE(sk == unlinked);
	UNLINK_DLIST(sk, llink);
	memset((char *)sk + offsetof(symkey, symkey_payload), '\0',
	       sizeof(*sk) - offsetof(symkey, symkey_payload));
	LINK_SLIST(authfreekeys, sk, llink.f);
	authnumkeys--;
	authnumfreekeys++;
}


/*
 * auth_findkey - find a key in the hash table
 */
struct savekey *
auth_findkey(
	keyid_t		id
	)
{
	symkey *	sk;

	for (sk = key_hash[KEYHASH(id)]; sk != NULL; sk = sk->hlink)
		if (id == sk->keyid)
			return sk;
	return NULL;
}


/*
 * auth_havekey - return TRUE if the key id is zero or known. The
 * key needs not to be trusted.
 */
int
auth_havekey(
	keyid_t		id
	)
{
	return
	    (0           == id) ||
	    (cache_keyid == id) ||
	    (NULL        != auth_findkey(id));
}


/*
 * authhavekey - return TRUE and cache the key, if zero or both known
 *		 and trusted.
 */
int
authhavekey(
	keyid_t		id
	)
{
	symkey *	sk;

	authkeylookups++;
	if (0 == id || cache_keyid == id)
		return !!(KEY_TRUSTED & cache_flags);

	/*
	 * Search the bin for the key. If not found, or found but the key
	 * type is zero, somebody marked it trusted without specifying a
	 * key or key type. In this case consider the key missing.
	 */
	authkeyuncached++;
	sk = auth_findkey(id);
	if ((sk == NULL) || (sk->type == 0)) {
		authkeynotfound++;
		return FALSE;
	}

	/*
	 * If the key is not trusted, the key is not considered found.
	 */
	if ( ! (KEY_TRUSTED & sk->flags)) {
		authnokey++;
		return FALSE;
	}

	/*
	 * The key is found and trusted. Initialize the key cache.
	 */
	cache_keyid = sk->keyid;
	cache_type = sk->type;
	cache_flags = sk->flags;
	cache_secret = sk->secret;
	cache_secretsize = sk->secretsize;
	cache_keyacclist = sk->keyacclist;

	return TRUE;
}


/*
 * authtrust - declare a key to be trusted/untrusted
 */
void
authtrust(
	keyid_t		id,
	u_long		trust
	)
{
	symkey *	sk;
	u_long		lifetime;

	/*
	 * Search bin for key; if it does not exist and is untrusted,
	 * forget it.
	 */

	sk = auth_findkey(id);
	if (!trust && sk == NULL)
		return;

	/*
	 * There are two conditions remaining. Either it does not
	 * exist and is to be trusted or it does exist and is or is
	 * not to be trusted.
	 */	
	if (sk != NULL) {
		/*
		 * Key exists. If it is to be trusted, say so and update
		 * its lifetime. If no longer trusted, return it to the
		 * free list. Flush the cache first to be sure there are
		 * no discrepancies.
		 */
		authcache_flush_id(id);
		if (trust > 0) {
			sk->flags |= KEY_TRUSTED;
			if (trust > 1)
				sk->lifetime = current_time + trust;
			else
				sk->lifetime = 0;
		} else {
			freesymkey(sk);
		}
		return;
	}

	/*
	 * keyid is not present, but the is to be trusted.  We allocate
	 * a new key, but do not specify a key type or secret.
	 */
	if (trust > 1) {
		lifetime = current_time + trust;
	} else {
		lifetime = 0;
	}
	allocsymkey(id, KEY_TRUSTED, 0, lifetime, 0, NULL, NULL);
}


/*
 * authistrusted - determine whether a key is trusted
 */
int
authistrusted(
	keyid_t		id
	)
{
	symkey *	sk;

	if (id == cache_keyid)
		return !!(KEY_TRUSTED & cache_flags);

	authkeyuncached++;
	sk = auth_findkey(id);
	if (sk == NULL || !(KEY_TRUSTED & sk->flags)) {
		authkeynotfound++;
		return FALSE;
	}
	return TRUE;
}


/*
 * authistrustedip - determine if the IP is OK for the keyid
 */
 int
 authistrustedip(
 	keyid_t		keyno,
	sockaddr_u *	sau
	)
{
	symkey *	sk;

	if (keyno == cache_keyid) {
		return (KEY_TRUSTED & cache_flags) &&
		    keyacc_contains(cache_keyacclist, sau, TRUE);
	}

	if (NULL != (sk = auth_findkey(keyno))) {
		authkeyuncached++;
		return (KEY_TRUSTED & sk->flags) &&
		    keyacc_contains(sk->keyacclist, sau, TRUE);
	}
	
	authkeynotfound++;
	return FALSE;    
}

/* Note: There are two locations below where 'strncpy()' is used. While
 * this function is a hazard by itself, it's essential that it is used
 * here. Bug 1243 involved that the secret was filled with NUL bytes
 * after the first NUL encountered, and 'strlcpy()' simply does NOT have
 * this behaviour. So disabling the fix and reverting to the buggy
 * behaviour due to compatibility issues MUST also fill with NUL and
 * this needs 'strncpy'. Also, the secret is managed as a byte blob of a
 * given size, and eventually truncating it and replacing the last byte
 * with a NUL would be a bug.
 * perlinger@ntp.org 2015-10-10
 */
void
MD5auth_setkey(
	keyid_t keyno,
	int	keytype,
	const u_char *key,
	size_t secretsize,
	KeyAccT *ka
	)
{
	symkey *	sk;
	u_char *	secret;
	
	DEBUG_ENSURE(keytype <= USHRT_MAX);
	DEBUG_ENSURE(secretsize < 4 * 1024);
	/*
	 * See if we already have the key.  If so just stick in the
	 * new value.
	 */
	sk = auth_findkey(keyno);
	if (sk != NULL && keyno == sk->keyid) {
			/* TALOS-CAN-0054: make sure we have a new buffer! */
		if (NULL != sk->secret) {
			memset(sk->secret, 0, sk->secretsize);
			free(sk->secret);
		}
		sk->secret = emalloc(secretsize + 1);
		sk->type = (u_short)keytype;
		sk->secretsize = secretsize;
		/* make sure access lists don't leak here! */
		if (ka != sk->keyacclist) {
			keyacc_all_free(sk->keyacclist);
			sk->keyacclist = ka;
		}
#ifndef DISABLE_BUG1243_FIX
		memcpy(sk->secret, key, secretsize);
#else
		/* >MUST< use 'strncpy()' here! See above! */
		strncpy((char *)sk->secret, (const char *)key,
			secretsize);
#endif
		authcache_flush_id(keyno);
		return;
	}

	/*
	 * Need to allocate new structure.  Do it.
	 */
	secret = emalloc(secretsize + 1);
#ifndef DISABLE_BUG1243_FIX
	memcpy(secret, key, secretsize);
#else
	/* >MUST< use 'strncpy()' here! See above! */
	strncpy((char *)secret, (const char *)key, secretsize);
#endif
	allocsymkey(keyno, 0, (u_short)keytype, 0,
		    secretsize, secret, ka);
#ifdef DEBUG
	if (debug >= 4) {
		size_t	j;

		printf("auth_setkey: key %d type %d len %d ", (int)keyno,
		    keytype, (int)secretsize);
		for (j = 0; j < secretsize; j++) {
			printf("%02x", secret[j]);
		}
		printf("\n");
	}	
#endif
}


/*
 * auth_delkeys - delete non-autokey untrusted keys, and clear all info
 *                except the trusted bit of non-autokey trusted keys, in
 *		  preparation for rereading the keys file.
 */
void
auth_delkeys(void)
{
	symkey *	sk;

	ITER_DLIST_BEGIN(key_listhead, sk, llink, symkey)
		if (sk->keyid > NTP_MAXKEY) {	/* autokey */
			continue;
		}

		/*
		 * Don't lose info as to which keys are trusted. Make
		 * sure there are no dangling pointers!
		 */
		if (KEY_TRUSTED & sk->flags) {
			if (sk->secret != NULL) {
				memset(sk->secret, 0, sk->secretsize);
				free(sk->secret);
				sk->secret = NULL; /* TALOS-CAN-0054 */
			}
			sk->keyacclist = keyacc_all_free(sk->keyacclist);
			sk->secretsize = 0;
			sk->lifetime = 0;
		} else {
			freesymkey(sk);
		}
	ITER_DLIST_END()
}


/*
 * auth_agekeys - delete keys whose lifetimes have expired
 */
void
auth_agekeys(void)
{
	symkey *	sk;

	ITER_DLIST_BEGIN(key_listhead, sk, llink, symkey)
		if (sk->lifetime > 0 && current_time > sk->lifetime) {
			freesymkey(sk);
			authkeyexpired++;
		}
	ITER_DLIST_END()
	DPRINTF(1, ("auth_agekeys: at %lu keys %lu expired %lu\n",
		    current_time, authnumkeys, authkeyexpired));
}


/*
 * authencrypt - generate message authenticator
 *
 * Returns length of authenticator field, zero if key not found.
 */
size_t
authencrypt(
	keyid_t		keyno,
	u_int32 *	pkt,
	size_t		length
	)
{
	/*
	 * A zero key identifier means the sender has not verified
	 * the last message was correctly authenticated. The MAC
	 * consists of a single word with value zero.
	 */
	authencryptions++;
	pkt[length / 4] = htonl(keyno);
	if (0 == keyno) {
		return 4;
	}
	if (!authhavekey(keyno)) {
		return 0;
	}

	return MD5authencrypt(cache_type,
			      cache_secret, cache_secretsize,
			      pkt, length);
}


/*
 * authdecrypt - verify message authenticator
 *
 * Returns TRUE if authenticator valid, FALSE if invalid or not found.
 */
int
authdecrypt(
	keyid_t		keyno,
	u_int32 *	pkt,
	size_t		length,
	size_t		size
	)
{
	/*
	 * A zero key identifier means the sender has not verified
	 * the last message was correctly authenticated.  For our
	 * purpose this is an invalid authenticator.
	 */
	authdecryptions++;
	if (0 == keyno || !authhavekey(keyno) || size < 4) {
		return FALSE;
	}

	return MD5authdecrypt(cache_type,
			      cache_secret, cache_secretsize,
			      pkt, length, size);
}
