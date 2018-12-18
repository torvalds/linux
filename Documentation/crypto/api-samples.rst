Code Examples
=============

Code Example For Symmetric Key Cipher Operation
-----------------------------------------------

::


    /* tie all data structures together */
    struct skcipher_def {
        struct scatterlist sg;
        struct crypto_skcipher *tfm;
        struct skcipher_request *req;
        struct crypto_wait wait;
    };

    /* Perform cipher operation */
    static unsigned int test_skcipher_encdec(struct skcipher_def *sk,
                         int enc)
    {
        int rc;

        if (enc)
            rc = crypto_wait_req(crypto_skcipher_encrypt(sk->req), &sk->wait);
        else
            rc = crypto_wait_req(crypto_skcipher_decrypt(sk->req), &sk->wait);

	if (rc)
		pr_info("skcipher encrypt returned with result %d\n", rc);

        return rc;
    }

    /* Initialize and trigger cipher operation */
    static int test_skcipher(void)
    {
        struct skcipher_def sk;
        struct crypto_skcipher *skcipher = NULL;
        struct skcipher_request *req = NULL;
        char *scratchpad = NULL;
        char *ivdata = NULL;
        unsigned char key[32];
        int ret = -EFAULT;

        skcipher = crypto_alloc_skcipher("cbc-aes-aesni", 0, 0);
        if (IS_ERR(skcipher)) {
            pr_info("could not allocate skcipher handle\n");
            return PTR_ERR(skcipher);
        }

        req = skcipher_request_alloc(skcipher, GFP_KERNEL);
        if (!req) {
            pr_info("could not allocate skcipher request\n");
            ret = -ENOMEM;
            goto out;
        }

        skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
                          crypto_req_done,
                          &sk.wait);

        /* AES 256 with random key */
        get_random_bytes(&key, 32);
        if (crypto_skcipher_setkey(skcipher, key, 32)) {
            pr_info("key could not be set\n");
            ret = -EAGAIN;
            goto out;
        }

        /* IV will be random */
        ivdata = kmalloc(16, GFP_KERNEL);
        if (!ivdata) {
            pr_info("could not allocate ivdata\n");
            goto out;
        }
        get_random_bytes(ivdata, 16);

        /* Input data will be random */
        scratchpad = kmalloc(16, GFP_KERNEL);
        if (!scratchpad) {
            pr_info("could not allocate scratchpad\n");
            goto out;
        }
        get_random_bytes(scratchpad, 16);

        sk.tfm = skcipher;
        sk.req = req;

        /* We encrypt one block */
        sg_init_one(&sk.sg, scratchpad, 16);
        skcipher_request_set_crypt(req, &sk.sg, &sk.sg, 16, ivdata);
        crypto_init_wait(&sk.wait);

        /* encrypt data */
        ret = test_skcipher_encdec(&sk, 1);
        if (ret)
            goto out;

        pr_info("Encryption triggered successfully\n");

    out:
        if (skcipher)
            crypto_free_skcipher(skcipher);
        if (req)
            skcipher_request_free(req);
        if (ivdata)
            kfree(ivdata);
        if (scratchpad)
            kfree(scratchpad);
        return ret;
    }


Code Example For Use of Operational State Memory With SHASH
-----------------------------------------------------------

::


    struct sdesc {
        struct shash_desc shash;
        char ctx[];
    };

    static struct sdesc *init_sdesc(struct crypto_shash *alg)
    {
        struct sdesc *sdesc;
        int size;

        size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
        sdesc = kmalloc(size, GFP_KERNEL);
        if (!sdesc)
            return ERR_PTR(-ENOMEM);
        sdesc->shash.tfm = alg;
        sdesc->shash.flags = 0x0;
        return sdesc;
    }

    static int calc_hash(struct crypto_shash *alg,
                 const unsigned char *data, unsigned int datalen,
                 unsigned char *digest)
    {
        struct sdesc *sdesc;
        int ret;

        sdesc = init_sdesc(alg);
        if (IS_ERR(sdesc)) {
            pr_info("can't alloc sdesc\n");
            return PTR_ERR(sdesc);
        }

        ret = crypto_shash_digest(&sdesc->shash, data, datalen, digest);
        kfree(sdesc);
        return ret;
    }

    static int test_hash(const unsigned char *data, unsigned int datalen,
                 unsigned char *digest)
    {
        struct crypto_shash *alg;
        char *hash_alg_name = "sha1-padlock-nano";
        int ret;

        alg = crypto_alloc_shash(hash_alg_name, 0, 0);
        if (IS_ERR(alg)) {
                pr_info("can't alloc alg %s\n", hash_alg_name);
                return PTR_ERR(alg);
        }
        ret = calc_hash(alg, data, datalen, digest);
        crypto_free_shash(alg);
        return ret;
    }


Code Example For Random Number Generator Usage
----------------------------------------------

::


    static int get_random_numbers(u8 *buf, unsigned int len)
    {
        struct crypto_rng *rng = NULL;
        char *drbg = "drbg_nopr_sha256"; /* Hash DRBG with SHA-256, no PR */
        int ret;

        if (!buf || !len) {
            pr_debug("No output buffer provided\n");
            return -EINVAL;
        }

        rng = crypto_alloc_rng(drbg, 0, 0);
        if (IS_ERR(rng)) {
            pr_debug("could not allocate RNG handle for %s\n", drbg);
            return PTR_ERR(rng);
        }

        ret = crypto_rng_get_bytes(rng, buf, len);
        if (ret < 0)
            pr_debug("generation of random numbers failed\n");
        else if (ret == 0)
            pr_debug("RNG returned no data");
        else
            pr_debug("RNG returned %d bytes of data\n", ret);

    out:
        crypto_free_rng(rng);
        return ret;
    }
