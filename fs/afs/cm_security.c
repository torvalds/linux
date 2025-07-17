// SPDX-License-Identifier: GPL-2.0-or-later
/* Cache manager security.
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <crypto/krb5.h>
#include "internal.h"
#include "afs_cm.h"
#include "afs_fs.h"
#include "protocol_yfs.h"
#define RXRPC_TRACE_ONLY_DEFINE_ENUMS
#include <trace/events/rxrpc.h>

#define RXGK_SERVER_ENC_TOKEN 1036U // 0x40c
#define xdr_round_up(x) (round_up((x), sizeof(__be32)))
#define xdr_len_object(x) (4 + round_up((x), sizeof(__be32)))

#ifdef CONFIG_RXGK
static int afs_create_yfs_cm_token(struct sk_buff *challenge,
				   struct afs_server *server);
#endif

/*
 * Respond to an RxGK challenge, adding appdata.
 */
static int afs_respond_to_challenge(struct sk_buff *challenge)
{
#ifdef CONFIG_RXGK
	struct krb5_buffer appdata = {};
	struct afs_server *server;
#endif
	struct rxrpc_peer *peer;
	unsigned long peer_data;
	u16 service_id;
	u8 security_index;

	rxrpc_kernel_query_challenge(challenge, &peer, &peer_data,
				     &service_id, &security_index);

	_enter("%u,%u", service_id, security_index);

	switch (service_id) {
		/* We don't send CM_SERVICE RPCs, so don't expect a challenge
		 * therefrom.
		 */
	case FS_SERVICE:
	case VL_SERVICE:
	case YFS_FS_SERVICE:
	case YFS_VL_SERVICE:
		break;
	default:
		pr_warn("Can't respond to unknown challenge %u:%u",
			service_id, security_index);
		return rxrpc_kernel_reject_challenge(challenge, RX_USER_ABORT, -EPROTO,
						     afs_abort_unsupported_sec_class);
	}

	switch (security_index) {
#ifdef CONFIG_RXKAD
	case RXRPC_SECURITY_RXKAD:
		return rxkad_kernel_respond_to_challenge(challenge);
#endif

#ifdef CONFIG_RXGK
	case RXRPC_SECURITY_RXGK:
		return rxgk_kernel_respond_to_challenge(challenge, &appdata);

	case RXRPC_SECURITY_YFS_RXGK:
		switch (service_id) {
		case FS_SERVICE:
		case YFS_FS_SERVICE:
			server = (struct afs_server *)peer_data;
			if (!server->cm_rxgk_appdata.data) {
				mutex_lock(&server->cm_token_lock);
				if (!server->cm_rxgk_appdata.data)
					afs_create_yfs_cm_token(challenge, server);
				mutex_unlock(&server->cm_token_lock);
			}
			if (server->cm_rxgk_appdata.data)
				appdata = server->cm_rxgk_appdata;
			break;
		}
		return rxgk_kernel_respond_to_challenge(challenge, &appdata);
#endif

	default:
		return rxrpc_kernel_reject_challenge(challenge, RX_USER_ABORT, -EPROTO,
						     afs_abort_unsupported_sec_class);
	}
}

/*
 * Process the OOB message queue, processing challenge packets.
 */
void afs_process_oob_queue(struct work_struct *work)
{
	struct afs_net *net = container_of(work, struct afs_net, rx_oob_work);
	struct sk_buff *oob;
	enum rxrpc_oob_type type;

	while ((oob = rxrpc_kernel_dequeue_oob(net->socket, &type))) {
		switch (type) {
		case RXRPC_OOB_CHALLENGE:
			afs_respond_to_challenge(oob);
			break;
		}
		rxrpc_kernel_free_oob(oob);
	}
}

#ifdef CONFIG_RXGK
/*
 * Create a securities keyring for the cache manager and attach a key to it for
 * the RxGK tokens we want to use to secure the callback connection back from
 * the fileserver.
 */
int afs_create_token_key(struct afs_net *net, struct socket *socket)
{
	const struct krb5_enctype *krb5;
	struct key *ring;
	key_ref_t key;
	char K0[32], *desc;
	int ret;

	ring = keyring_alloc("kafs",
			     GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, current_cred(),
			     KEY_POS_SEARCH | KEY_POS_WRITE |
			     KEY_USR_VIEW | KEY_USR_READ | KEY_USR_SEARCH,
			     KEY_ALLOC_NOT_IN_QUOTA,
			     NULL, NULL);
	if (IS_ERR(ring))
		return PTR_ERR(ring);

	ret = rxrpc_sock_set_security_keyring(socket->sk, ring);
	if (ret < 0)
		goto out;

	ret = -ENOPKG;
	krb5 = crypto_krb5_find_enctype(KRB5_ENCTYPE_AES128_CTS_HMAC_SHA1_96);
	if (!krb5)
		goto out;

	if (WARN_ON_ONCE(krb5->key_len > sizeof(K0)))
		goto out;

	ret = -ENOMEM;
	desc = kasprintf(GFP_KERNEL, "%u:%u:%u:%u",
			 YFS_CM_SERVICE, RXRPC_SECURITY_YFS_RXGK, 1, krb5->etype);
	if (!desc)
		goto out;

	wait_for_random_bytes();
	get_random_bytes(K0, krb5->key_len);

	key = key_create(make_key_ref(ring, true),
			 "rxrpc_s", desc,
			 K0, krb5->key_len,
			 KEY_POS_VIEW | KEY_POS_READ | KEY_POS_SEARCH | KEY_USR_VIEW,
			 KEY_ALLOC_NOT_IN_QUOTA);
	kfree(desc);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto out;
	}

	net->fs_cm_token_key = key_ref_to_ptr(key);
	ret = 0;
out:
	key_put(ring);
	return ret;
}

/*
 * Create an YFS RxGK GSS token to use as a ticket to the specified fileserver.
 */
static int afs_create_yfs_cm_token(struct sk_buff *challenge,
				   struct afs_server *server)
{
	const struct krb5_enctype *conn_krb5, *token_krb5;
	const struct krb5_buffer *token_key;
	struct crypto_aead *aead;
	struct scatterlist sg;
	struct afs_net *net = server->cell->net;
	const struct key *key = net->fs_cm_token_key;
	size_t keysize, uuidsize, authsize, toksize, encsize, contsize, adatasize, offset;
	__be32 caps[1] = {
		[0] = htonl(AFS_CAP_ERROR_TRANSLATION),
	};
	__be32 *xdr;
	void *appdata, *K0, *encbase;
	u32 enctype;
	int ret;

	if (!key)
		return -ENOKEY;

	/* Assume that the fileserver is happy to use the same encoding type as
	 * we were told to use by the token obtained by the user.
	 */
	enctype = rxgk_kernel_query_challenge(challenge);

	conn_krb5 = crypto_krb5_find_enctype(enctype);
	if (!conn_krb5)
		return -ENOPKG;
	token_krb5 = key->payload.data[0];
	token_key = (const struct krb5_buffer *)&key->payload.data[2];

	/* struct rxgk_key {
	 *	afs_uint32	enctype;
	 *	opaque		key<>;
	 * };
	 */
	keysize = 4 + xdr_len_object(conn_krb5->key_len);

	/* struct RXGK_AuthName {
	 *	afs_int32	kind;
	 *	opaque		data<AUTHDATAMAX>;
	 *	opaque		display<AUTHPRINTABLEMAX>;
	 * };
	 */
	uuidsize = sizeof(server->uuid);
	authsize = 4 + xdr_len_object(uuidsize) + xdr_len_object(0);

	/* struct RXGK_Token {
	 *	rxgk_key		K0;
	 *	RXGK_Level		level;
	 *	rxgkTime		starttime;
	 *	afs_int32		lifetime;
	 *	afs_int32		bytelife;
	 *	rxgkTime		expirationtime;
	 *	struct RXGK_AuthName	identities<>;
	 * };
	 */
	toksize = keysize + 8 + 4 + 4 + 8 + xdr_len_object(authsize);

	offset = 0;
	encsize = crypto_krb5_how_much_buffer(token_krb5, KRB5_ENCRYPT_MODE, toksize, &offset);

	/* struct RXGK_TokenContainer {
	 *	afs_int32	kvno;
	 *	afs_int32	enctype;
	 *	opaque		encrypted_token<>;
	 * };
	 */
	contsize = 4 + 4 + xdr_len_object(encsize);

	/* struct YFSAppData {
	 *	opr_uuid	initiatorUuid;
	 *	opr_uuid	acceptorUuid;
	 *	Capabilities	caps;
	 *	afs_int32	enctype;
	 *	opaque		callbackKey<>;
	 *	opaque		callbackToken<>;
	 * };
	 */
	adatasize = 16 + 16 +
		xdr_len_object(sizeof(caps)) +
		4 +
		xdr_len_object(conn_krb5->key_len) +
		xdr_len_object(contsize);

	ret = -ENOMEM;
	appdata = kzalloc(adatasize, GFP_KERNEL);
	if (!appdata)
		goto out;
	xdr = appdata;

	memcpy(xdr, &net->uuid, 16);		/* appdata.initiatorUuid */
	xdr += 16 / 4;
	memcpy(xdr, &server->uuid, 16);		/* appdata.acceptorUuid */
	xdr += 16 / 4;
	*xdr++ = htonl(ARRAY_SIZE(caps));	/* appdata.caps.len */
	memcpy(xdr, &caps, sizeof(caps));	/* appdata.caps */
	xdr += ARRAY_SIZE(caps);
	*xdr++ = htonl(conn_krb5->etype);	/* appdata.enctype */

	*xdr++ = htonl(conn_krb5->key_len);	/* appdata.callbackKey.len */
	K0 = xdr;
	get_random_bytes(K0, conn_krb5->key_len); /* appdata.callbackKey.data */
	xdr += xdr_round_up(conn_krb5->key_len) / 4;

	*xdr++ = htonl(contsize);		/* appdata.callbackToken.len */
	*xdr++ = htonl(1);			/* cont.kvno */
	*xdr++ = htonl(token_krb5->etype);	/* cont.enctype */
	*xdr++ = htonl(encsize);		/* cont.encrypted_token.len */

	encbase = xdr;
	xdr += offset / 4;
	*xdr++ = htonl(conn_krb5->etype);	/* token.K0.enctype */
	*xdr++ = htonl(conn_krb5->key_len);	/* token.K0.key.len */
	memcpy(xdr, K0, conn_krb5->key_len);	/* token.K0.key.data */
	xdr += xdr_round_up(conn_krb5->key_len) / 4;

	*xdr++ = htonl(RXRPC_SECURITY_ENCRYPT);	/* token.level */
	*xdr++ = htonl(0);			/* token.starttime */
	*xdr++ = htonl(0);			/* " */
	*xdr++ = htonl(0);			/* token.lifetime */
	*xdr++ = htonl(0);			/* token.bytelife */
	*xdr++ = htonl(0);			/* token.expirationtime */
	*xdr++ = htonl(0);			/* " */
	*xdr++ = htonl(1);			/* token.identities.count */
	*xdr++ = htonl(0);			/* token.identities[0].kind */
	*xdr++ = htonl(uuidsize);		/* token.identities[0].data.len */
	memcpy(xdr, &server->uuid, uuidsize);
	xdr += xdr_round_up(uuidsize) / 4;
	*xdr++ = htonl(0);			/* token.identities[0].display.len */

	xdr = encbase + xdr_round_up(encsize);

	if ((unsigned long)xdr - (unsigned long)appdata != adatasize)
		pr_err("Appdata size incorrect %lx != %zx\n",
		       (unsigned long)xdr - (unsigned long)appdata, adatasize);

	aead = crypto_krb5_prepare_encryption(token_krb5, token_key, RXGK_SERVER_ENC_TOKEN,
					      GFP_KERNEL);
	if (IS_ERR(aead)) {
		ret = PTR_ERR(aead);
		goto out_token;
	}

	sg_init_one(&sg, encbase, encsize);
	ret = crypto_krb5_encrypt(token_krb5, aead, &sg, 1, encsize, offset, toksize, false);
	if (ret < 0)
		goto out_aead;

	server->cm_rxgk_appdata.len  = adatasize;
	server->cm_rxgk_appdata.data = appdata;
	appdata = NULL;

out_aead:
	crypto_free_aead(aead);
out_token:
	kfree(appdata);
out:
	return ret;
}
#endif /* CONFIG_RXGK */
