/*
 * Copyright 2004-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "pcy_int.h"

/*
 * Enable this to print out the complete policy tree at various point during
 * evaluation.
 */

/*
 * #define OPENSSL_POLICY_DEBUG
 */

#ifdef OPENSSL_POLICY_DEBUG

static void expected_print(BIO *err, X509_POLICY_LEVEL *lev,
                           X509_POLICY_NODE *node, int indent)
{
    if ((lev->flags & X509_V_FLAG_INHIBIT_MAP)
        || !(node->data->flags & POLICY_DATA_FLAG_MAP_MASK))
        BIO_puts(err, "  Not Mapped\n");
    else {
        int i;
        STACK_OF(ASN1_OBJECT) *pset = node->data->expected_policy_set;
        ASN1_OBJECT *oid;
        BIO_puts(err, "  Expected: ");
        for (i = 0; i < sk_ASN1_OBJECT_num(pset); i++) {
            oid = sk_ASN1_OBJECT_value(pset, i);
            if (i)
                BIO_puts(err, ", ");
            i2a_ASN1_OBJECT(err, oid);
        }
        BIO_puts(err, "\n");
    }
}

static void tree_print(char *str, X509_POLICY_TREE *tree,
                       X509_POLICY_LEVEL *curr)
{
    BIO *err = BIO_new_fp(stderr, BIO_NOCLOSE);
    X509_POLICY_LEVEL *plev;

    if (err == NULL)
        return;
    if (!curr)
        curr = tree->levels + tree->nlevel;
    else
        curr++;

    BIO_printf(err, "Level print after %s\n", str);
    BIO_printf(err, "Printing Up to Level %ld\n", curr - tree->levels);
    for (plev = tree->levels; plev != curr; plev++) {
        int i;

        BIO_printf(err, "Level %ld, flags = %x\n",
                   (long)(plev - tree->levels), plev->flags);
        for (i = 0; i < sk_X509_POLICY_NODE_num(plev->nodes); i++) {
            X509_POLICY_NODE *node = sk_X509_POLICY_NODE_value(plev->nodes, i);

            X509_POLICY_NODE_print(err, node, 2);
            expected_print(err, plev, node, 2);
            BIO_printf(err, "  Flags: %x\n", node->data->flags);
        }
        if (plev->anyPolicy)
            X509_POLICY_NODE_print(err, plev->anyPolicy, 2);
    }
    BIO_free(err);
}
#endif

/*-
 * Return value: <= 0 on error, or positive bit mask:
 *
 * X509_PCY_TREE_VALID: valid tree
 * X509_PCY_TREE_EMPTY: empty tree (including bare TA case)
 * X509_PCY_TREE_EXPLICIT: explicit policy required
 */
static int tree_init(X509_POLICY_TREE **ptree, STACK_OF(X509) *certs,
                     unsigned int flags)
{
    X509_POLICY_TREE *tree;
    X509_POLICY_LEVEL *level;
    const X509_POLICY_CACHE *cache;
    X509_POLICY_DATA *data = NULL;
    int ret = X509_PCY_TREE_VALID;
    int n = sk_X509_num(certs) - 1; /* RFC5280 paths omit the TA */
    int explicit_policy = (flags & X509_V_FLAG_EXPLICIT_POLICY) ? 0 : n+1;
    int any_skip = (flags & X509_V_FLAG_INHIBIT_ANY) ? 0 : n+1;
    int map_skip = (flags & X509_V_FLAG_INHIBIT_MAP) ? 0 : n+1;
    int i;

    *ptree = NULL;

    /* Can't do anything with just a trust anchor */
    if (n == 0)
        return X509_PCY_TREE_EMPTY;

    /*
     * First setup the policy cache in all n non-TA certificates, this will be
     * used in X509_verify_cert() which will invoke the verify callback for all
     * certificates with invalid policy extensions.
     */
    for (i = n - 1; i >= 0; i--) {
        X509 *x = sk_X509_value(certs, i);

        /* Call for side-effect of computing hash and caching extensions */
        X509_check_purpose(x, -1, 0);

        /* If cache is NULL, likely ENOMEM: return immediately */
        if (policy_cache_set(x) == NULL)
            return X509_PCY_TREE_INTERNAL;
    }

    /*
     * At this point check for invalid policies and required explicit policy.
     * Note that the explicit_policy counter is a count-down to zero, with the
     * requirement kicking in if and once it does that.  The counter is
     * decremented for every non-self-issued certificate in the path, but may
     * be further reduced by policy constraints in a non-leaf certificate.
     *
     * The ultimate policy set is the intersection of all the policies along
     * the path, if we hit a certificate with an empty policy set, and explicit
     * policy is required we're done.
     */
    for (i = n - 1;
         i >= 0 && (explicit_policy > 0 || (ret & X509_PCY_TREE_EMPTY) == 0);
         i--) {
        X509 *x = sk_X509_value(certs, i);
        uint32_t ex_flags = X509_get_extension_flags(x);

        /* All the policies are already cached, we can return early */
        if (ex_flags & EXFLAG_INVALID_POLICY)
            return X509_PCY_TREE_INVALID;

        /* Access the cache which we now know exists */
        cache = policy_cache_set(x);

        if ((ret & X509_PCY_TREE_VALID) && cache->data == NULL)
            ret = X509_PCY_TREE_EMPTY;
        if (explicit_policy > 0) {
            if (!(ex_flags & EXFLAG_SI))
                explicit_policy--;
            if ((cache->explicit_skip >= 0)
                && (cache->explicit_skip < explicit_policy))
                explicit_policy = cache->explicit_skip;
        }
    }

    if (explicit_policy == 0)
        ret |= X509_PCY_TREE_EXPLICIT;
    if ((ret & X509_PCY_TREE_VALID) == 0)
        return ret;

    /* If we get this far initialize the tree */
    if ((tree = OPENSSL_zalloc(sizeof(*tree))) == NULL) {
        X509V3err(X509V3_F_TREE_INIT, ERR_R_MALLOC_FAILURE);
        return X509_PCY_TREE_INTERNAL;
    }

    /*
     * http://tools.ietf.org/html/rfc5280#section-6.1.2, figure 3.
     *
     * The top level is implicitly for the trust anchor with valid expected
     * policies of anyPolicy.  (RFC 5280 has the TA at depth 0 and the leaf at
     * depth n, we have the leaf at depth 0 and the TA at depth n).
     */
    if ((tree->levels = OPENSSL_zalloc(sizeof(*tree->levels)*(n+1))) == NULL) {
        OPENSSL_free(tree);
        X509V3err(X509V3_F_TREE_INIT, ERR_R_MALLOC_FAILURE);
        return X509_PCY_TREE_INTERNAL;
    }
    tree->nlevel = n+1;
    level = tree->levels;
    if ((data = policy_data_new(NULL, OBJ_nid2obj(NID_any_policy), 0)) == NULL)
        goto bad_tree;
    if (level_add_node(level, data, NULL, tree) == NULL) {
        policy_data_free(data);
        goto bad_tree;
    }

    /*
     * In this pass initialize all the tree levels and whether anyPolicy and
     * policy mapping are inhibited at each level.
     */
    for (i = n - 1; i >= 0; i--) {
        X509 *x = sk_X509_value(certs, i);
        uint32_t ex_flags = X509_get_extension_flags(x);

        /* Access the cache which we now know exists */
        cache = policy_cache_set(x);

        X509_up_ref(x);
        (++level)->cert = x;

        if (!cache->anyPolicy)
            level->flags |= X509_V_FLAG_INHIBIT_ANY;

        /* Determine inhibit any and inhibit map flags */
        if (any_skip == 0) {
            /*
             * Any matching allowed only if certificate is self issued and not
             * the last in the chain.
             */
            if (!(ex_flags & EXFLAG_SI) || (i == 0))
                level->flags |= X509_V_FLAG_INHIBIT_ANY;
        } else {
            if (!(ex_flags & EXFLAG_SI))
                any_skip--;
            if ((cache->any_skip >= 0) && (cache->any_skip < any_skip))
                any_skip = cache->any_skip;
        }

        if (map_skip == 0)
            level->flags |= X509_V_FLAG_INHIBIT_MAP;
        else {
            if (!(ex_flags & EXFLAG_SI))
                map_skip--;
            if ((cache->map_skip >= 0) && (cache->map_skip < map_skip))
                map_skip = cache->map_skip;
        }
    }

    *ptree = tree;
    return ret;

 bad_tree:
    X509_policy_tree_free(tree);
    return X509_PCY_TREE_INTERNAL;
}

/*
 * Return value: 1 on success, 0 otherwise
 */
static int tree_link_matching_nodes(X509_POLICY_LEVEL *curr,
                                    X509_POLICY_DATA *data)
{
    X509_POLICY_LEVEL *last = curr - 1;
    int i, matched = 0;

    /* Iterate through all in nodes linking matches */
    for (i = 0; i < sk_X509_POLICY_NODE_num(last->nodes); i++) {
        X509_POLICY_NODE *node = sk_X509_POLICY_NODE_value(last->nodes, i);

        if (policy_node_match(last, node, data->valid_policy)) {
            if (level_add_node(curr, data, node, NULL) == NULL)
                return 0;
            matched = 1;
        }
    }
    if (!matched && last->anyPolicy) {
        if (level_add_node(curr, data, last->anyPolicy, NULL) == NULL)
            return 0;
    }
    return 1;
}

/*
 * This corresponds to RFC3280 6.1.3(d)(1): link any data from
 * CertificatePolicies onto matching parent or anyPolicy if no match.
 *
 * Return value: 1 on success, 0 otherwise.
 */
static int tree_link_nodes(X509_POLICY_LEVEL *curr,
                           const X509_POLICY_CACHE *cache)
{
    int i;

    for (i = 0; i < sk_X509_POLICY_DATA_num(cache->data); i++) {
        X509_POLICY_DATA *data = sk_X509_POLICY_DATA_value(cache->data, i);

        /* Look for matching nodes in previous level */
        if (!tree_link_matching_nodes(curr, data))
            return 0;
    }
    return 1;
}

/*
 * This corresponds to RFC3280 6.1.3(d)(2): Create new data for any unmatched
 * policies in the parent and link to anyPolicy.
 *
 * Return value: 1 on success, 0 otherwise.
 */
static int tree_add_unmatched(X509_POLICY_LEVEL *curr,
                              const X509_POLICY_CACHE *cache,
                              const ASN1_OBJECT *id,
                              X509_POLICY_NODE *node, X509_POLICY_TREE *tree)
{
    X509_POLICY_DATA *data;

    if (id == NULL)
        id = node->data->valid_policy;
    /*
     * Create a new node with qualifiers from anyPolicy and id from unmatched
     * node.
     */
    if ((data = policy_data_new(NULL, id, node_critical(node))) == NULL)
        return 0;

    /* Curr may not have anyPolicy */
    data->qualifier_set = cache->anyPolicy->qualifier_set;
    data->flags |= POLICY_DATA_FLAG_SHARED_QUALIFIERS;
    if (level_add_node(curr, data, node, tree) == NULL) {
        policy_data_free(data);
        return 0;
    }
    return 1;
}

/*
 * Return value: 1 on success, 0 otherwise.
 */
static int tree_link_unmatched(X509_POLICY_LEVEL *curr,
                               const X509_POLICY_CACHE *cache,
                               X509_POLICY_NODE *node, X509_POLICY_TREE *tree)
{
    const X509_POLICY_LEVEL *last = curr - 1;
    int i;

    if ((last->flags & X509_V_FLAG_INHIBIT_MAP)
        || !(node->data->flags & POLICY_DATA_FLAG_MAPPED)) {
        /* If no policy mapping: matched if one child present */
        if (node->nchild)
            return 1;
        if (!tree_add_unmatched(curr, cache, NULL, node, tree))
            return 0;
        /* Add it */
    } else {
        /* If mapping: matched if one child per expected policy set */
        STACK_OF(ASN1_OBJECT) *expset = node->data->expected_policy_set;
        if (node->nchild == sk_ASN1_OBJECT_num(expset))
            return 1;
        /* Locate unmatched nodes */
        for (i = 0; i < sk_ASN1_OBJECT_num(expset); i++) {
            ASN1_OBJECT *oid = sk_ASN1_OBJECT_value(expset, i);
            if (level_find_node(curr, node, oid))
                continue;
            if (!tree_add_unmatched(curr, cache, oid, node, tree))
                return 0;
        }

    }
    return 1;
}

/*
 * Return value: 1 on success, 0 otherwise
 */
static int tree_link_any(X509_POLICY_LEVEL *curr,
                         const X509_POLICY_CACHE *cache,
                         X509_POLICY_TREE *tree)
{
    int i;
    X509_POLICY_NODE *node;
    X509_POLICY_LEVEL *last = curr - 1;

    for (i = 0; i < sk_X509_POLICY_NODE_num(last->nodes); i++) {
        node = sk_X509_POLICY_NODE_value(last->nodes, i);

        if (!tree_link_unmatched(curr, cache, node, tree))
            return 0;
    }
    /* Finally add link to anyPolicy */
    if (last->anyPolicy &&
        level_add_node(curr, cache->anyPolicy, last->anyPolicy, NULL) == NULL)
        return 0;
    return 1;
}

/*-
 * Prune the tree: delete any child mapped child data on the current level then
 * proceed up the tree deleting any data with no children. If we ever have no
 * data on a level we can halt because the tree will be empty.
 *
 * Return value: <= 0 error, otherwise one of:
 *
 * X509_PCY_TREE_VALID: valid tree
 * X509_PCY_TREE_EMPTY: empty tree
 */
static int tree_prune(X509_POLICY_TREE *tree, X509_POLICY_LEVEL *curr)
{
    STACK_OF(X509_POLICY_NODE) *nodes;
    X509_POLICY_NODE *node;
    int i;
    nodes = curr->nodes;
    if (curr->flags & X509_V_FLAG_INHIBIT_MAP) {
        for (i = sk_X509_POLICY_NODE_num(nodes) - 1; i >= 0; i--) {
            node = sk_X509_POLICY_NODE_value(nodes, i);
            /* Delete any mapped data: see RFC3280 XXXX */
            if (node->data->flags & POLICY_DATA_FLAG_MAP_MASK) {
                node->parent->nchild--;
                OPENSSL_free(node);
                (void)sk_X509_POLICY_NODE_delete(nodes, i);
            }
        }
    }

    for (;;) {
        --curr;
        nodes = curr->nodes;
        for (i = sk_X509_POLICY_NODE_num(nodes) - 1; i >= 0; i--) {
            node = sk_X509_POLICY_NODE_value(nodes, i);
            if (node->nchild == 0) {
                node->parent->nchild--;
                OPENSSL_free(node);
                (void)sk_X509_POLICY_NODE_delete(nodes, i);
            }
        }
        if (curr->anyPolicy && !curr->anyPolicy->nchild) {
            if (curr->anyPolicy->parent)
                curr->anyPolicy->parent->nchild--;
            OPENSSL_free(curr->anyPolicy);
            curr->anyPolicy = NULL;
        }
        if (curr == tree->levels) {
            /* If we zapped anyPolicy at top then tree is empty */
            if (!curr->anyPolicy)
                return X509_PCY_TREE_EMPTY;
            break;
        }
    }
    return X509_PCY_TREE_VALID;
}

/*
 * Return value: 1 on success, 0 otherwise.
 */
static int tree_add_auth_node(STACK_OF(X509_POLICY_NODE) **pnodes,
                              X509_POLICY_NODE *pcy)
{
    if (*pnodes == NULL &&
        (*pnodes = policy_node_cmp_new()) == NULL)
        return 0;
    if (sk_X509_POLICY_NODE_find(*pnodes, pcy) >= 0)
        return 1;
    return sk_X509_POLICY_NODE_push(*pnodes, pcy) != 0;
}

#define TREE_CALC_FAILURE 0
#define TREE_CALC_OK_NOFREE 1
#define TREE_CALC_OK_DOFREE 2

/*-
 * Calculate the authority set based on policy tree. The 'pnodes' parameter is
 * used as a store for the set of policy nodes used to calculate the user set.
 * If the authority set is not anyPolicy then pnodes will just point to the
 * authority set. If however the authority set is anyPolicy then the set of
 * valid policies (other than anyPolicy) is store in pnodes.
 *
 * Return value:
 *  TREE_CALC_FAILURE on failure,
 *  TREE_CALC_OK_NOFREE on success and pnodes need not be freed,
 *  TREE_CALC_OK_DOFREE on success and pnodes needs to be freed
 */
static int tree_calculate_authority_set(X509_POLICY_TREE *tree,
                                        STACK_OF(X509_POLICY_NODE) **pnodes)
{
    X509_POLICY_LEVEL *curr;
    X509_POLICY_NODE *node, *anyptr;
    STACK_OF(X509_POLICY_NODE) **addnodes;
    int i, j;
    curr = tree->levels + tree->nlevel - 1;

    /* If last level contains anyPolicy set is anyPolicy */
    if (curr->anyPolicy) {
        if (!tree_add_auth_node(&tree->auth_policies, curr->anyPolicy))
            return TREE_CALC_FAILURE;
        addnodes = pnodes;
    } else
        /* Add policies to authority set */
        addnodes = &tree->auth_policies;

    curr = tree->levels;
    for (i = 1; i < tree->nlevel; i++) {
        /*
         * If no anyPolicy node on this this level it can't appear on lower
         * levels so end search.
         */
        if ((anyptr = curr->anyPolicy) == NULL)
            break;
        curr++;
        for (j = 0; j < sk_X509_POLICY_NODE_num(curr->nodes); j++) {
            node = sk_X509_POLICY_NODE_value(curr->nodes, j);
            if ((node->parent == anyptr)
                && !tree_add_auth_node(addnodes, node)) {
                if (addnodes == pnodes) {
                    sk_X509_POLICY_NODE_free(*pnodes);
                    *pnodes = NULL;
                }
                return TREE_CALC_FAILURE;
            }
        }
    }
    if (addnodes == pnodes)
        return TREE_CALC_OK_DOFREE;

    *pnodes = tree->auth_policies;
    return TREE_CALC_OK_NOFREE;
}

/*
 * Return value: 1 on success, 0 otherwise.
 */
static int tree_calculate_user_set(X509_POLICY_TREE *tree,
                                   STACK_OF(ASN1_OBJECT) *policy_oids,
                                   STACK_OF(X509_POLICY_NODE) *auth_nodes)
{
    int i;
    X509_POLICY_NODE *node;
    ASN1_OBJECT *oid;
    X509_POLICY_NODE *anyPolicy;
    X509_POLICY_DATA *extra;

    /*
     * Check if anyPolicy present in authority constrained policy set: this
     * will happen if it is a leaf node.
     */
    if (sk_ASN1_OBJECT_num(policy_oids) <= 0)
        return 1;

    anyPolicy = tree->levels[tree->nlevel - 1].anyPolicy;

    for (i = 0; i < sk_ASN1_OBJECT_num(policy_oids); i++) {
        oid = sk_ASN1_OBJECT_value(policy_oids, i);
        if (OBJ_obj2nid(oid) == NID_any_policy) {
            tree->flags |= POLICY_FLAG_ANY_POLICY;
            return 1;
        }
    }

    for (i = 0; i < sk_ASN1_OBJECT_num(policy_oids); i++) {
        oid = sk_ASN1_OBJECT_value(policy_oids, i);
        node = tree_find_sk(auth_nodes, oid);
        if (!node) {
            if (!anyPolicy)
                continue;
            /*
             * Create a new node with policy ID from user set and qualifiers
             * from anyPolicy.
             */
            extra = policy_data_new(NULL, oid, node_critical(anyPolicy));
            if (extra == NULL)
                return 0;
            extra->qualifier_set = anyPolicy->data->qualifier_set;
            extra->flags = POLICY_DATA_FLAG_SHARED_QUALIFIERS
                | POLICY_DATA_FLAG_EXTRA_NODE;
            node = level_add_node(NULL, extra, anyPolicy->parent, tree);
        }
        if (!tree->user_policies) {
            tree->user_policies = sk_X509_POLICY_NODE_new_null();
            if (!tree->user_policies)
                return 1;
        }
        if (!sk_X509_POLICY_NODE_push(tree->user_policies, node))
            return 0;
    }
    return 1;
}

/*-
 * Return value: <= 0 error, otherwise one of:
 *  X509_PCY_TREE_VALID: valid tree
 *  X509_PCY_TREE_EMPTY: empty tree
 * (see tree_prune()).
 */
static int tree_evaluate(X509_POLICY_TREE *tree)
{
    int ret, i;
    X509_POLICY_LEVEL *curr = tree->levels + 1;
    const X509_POLICY_CACHE *cache;

    for (i = 1; i < tree->nlevel; i++, curr++) {
        cache = policy_cache_set(curr->cert);
        if (!tree_link_nodes(curr, cache))
            return X509_PCY_TREE_INTERNAL;

        if (!(curr->flags & X509_V_FLAG_INHIBIT_ANY)
            && !tree_link_any(curr, cache, tree))
            return X509_PCY_TREE_INTERNAL;
#ifdef OPENSSL_POLICY_DEBUG
        tree_print("before tree_prune()", tree, curr);
#endif
        ret = tree_prune(tree, curr);
        if (ret != X509_PCY_TREE_VALID)
            return ret;
    }
    return X509_PCY_TREE_VALID;
}

static void exnode_free(X509_POLICY_NODE *node)
{
    if (node->data && (node->data->flags & POLICY_DATA_FLAG_EXTRA_NODE))
        OPENSSL_free(node);
}

void X509_policy_tree_free(X509_POLICY_TREE *tree)
{
    X509_POLICY_LEVEL *curr;
    int i;

    if (!tree)
        return;

    sk_X509_POLICY_NODE_free(tree->auth_policies);
    sk_X509_POLICY_NODE_pop_free(tree->user_policies, exnode_free);

    for (i = 0, curr = tree->levels; i < tree->nlevel; i++, curr++) {
        X509_free(curr->cert);
        sk_X509_POLICY_NODE_pop_free(curr->nodes, policy_node_free);
        policy_node_free(curr->anyPolicy);
    }

    sk_X509_POLICY_DATA_pop_free(tree->extra_data, policy_data_free);
    OPENSSL_free(tree->levels);
    OPENSSL_free(tree);

}

/*-
 * Application policy checking function.
 * Return codes:
 *  X509_PCY_TREE_FAILURE:  Failure to satisfy explicit policy
 *  X509_PCY_TREE_INVALID:  Inconsistent or invalid extensions
 *  X509_PCY_TREE_INTERNAL: Internal error, most likely malloc
 *  X509_PCY_TREE_VALID:    Success (null tree if empty or bare TA)
 */
int X509_policy_check(X509_POLICY_TREE **ptree, int *pexplicit_policy,
                      STACK_OF(X509) *certs,
                      STACK_OF(ASN1_OBJECT) *policy_oids, unsigned int flags)
{
    int init_ret;
    int ret;
    int calc_ret;
    X509_POLICY_TREE *tree = NULL;
    STACK_OF(X509_POLICY_NODE) *nodes, *auth_nodes = NULL;

    *ptree = NULL;
    *pexplicit_policy = 0;
    init_ret = tree_init(&tree, certs, flags);

    if (init_ret <= 0)
        return init_ret;

    if ((init_ret & X509_PCY_TREE_EXPLICIT) == 0) {
        if (init_ret & X509_PCY_TREE_EMPTY) {
            X509_policy_tree_free(tree);
            return X509_PCY_TREE_VALID;
        }
    } else {
        *pexplicit_policy = 1;
        /* Tree empty and requireExplicit True: Error */
        if (init_ret & X509_PCY_TREE_EMPTY)
            return X509_PCY_TREE_FAILURE;
    }

    ret = tree_evaluate(tree);
#ifdef OPENSSL_POLICY_DEBUG
    tree_print("tree_evaluate()", tree, NULL);
#endif
    if (ret <= 0)
        goto error;

    if (ret == X509_PCY_TREE_EMPTY) {
        X509_policy_tree_free(tree);
        if (init_ret & X509_PCY_TREE_EXPLICIT)
            return X509_PCY_TREE_FAILURE;
        return X509_PCY_TREE_VALID;
    }

    /* Tree is not empty: continue */

    if ((calc_ret = tree_calculate_authority_set(tree, &auth_nodes)) == 0)
        goto error;
    ret = tree_calculate_user_set(tree, policy_oids, auth_nodes);
    if (calc_ret == TREE_CALC_OK_DOFREE)
        sk_X509_POLICY_NODE_free(auth_nodes);
    if (!ret)
        goto error;

    *ptree = tree;

    if (init_ret & X509_PCY_TREE_EXPLICIT) {
        nodes = X509_policy_tree_get0_user_policies(tree);
        if (sk_X509_POLICY_NODE_num(nodes) <= 0)
            return X509_PCY_TREE_FAILURE;
    }
    return X509_PCY_TREE_VALID;

 error:
    X509_policy_tree_free(tree);
    return X509_PCY_TREE_INTERNAL;
}
