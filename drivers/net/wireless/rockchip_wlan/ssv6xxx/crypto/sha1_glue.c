/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/sha.h>
#include <asm/byteorder.h>
struct SHA1_CTX {
 uint32_t h0,h1,h2,h3,h4;
 u64 count;
 u8 data[SHA1_BLOCK_SIZE];
};
asmlinkage void sha1_block_data_order(struct SHA1_CTX *digest,
  const unsigned char *data, unsigned int rounds);
static int sha1_init(struct shash_desc *desc)
{
 struct SHA1_CTX *sctx = shash_desc_ctx(desc);
 memset(sctx, 0, sizeof(*sctx));
 sctx->h0 = SHA1_H0;
 sctx->h1 = SHA1_H1;
 sctx->h2 = SHA1_H2;
 sctx->h3 = SHA1_H3;
 sctx->h4 = SHA1_H4;
 return 0;
}
static int __sha1_update(struct SHA1_CTX *sctx, const u8 *data,
          unsigned int len, unsigned int partial)
{
 unsigned int done = 0;
 sctx->count += len;
 if (partial) {
  done = SHA1_BLOCK_SIZE - partial;
  memcpy(sctx->data + partial, data, done);
  sha1_block_data_order(sctx, sctx->data, 1);
 }
 if (len - done >= SHA1_BLOCK_SIZE) {
  const unsigned int rounds = (len - done) / SHA1_BLOCK_SIZE;
  sha1_block_data_order(sctx, data + done, rounds);
  done += rounds * SHA1_BLOCK_SIZE;
 }
 memcpy(sctx->data, data + done, len - done);
 return 0;
}
static int sha1_update(struct shash_desc *desc, const u8 *data,
        unsigned int len)
{
 struct SHA1_CTX *sctx = shash_desc_ctx(desc);
 unsigned int partial = sctx->count % SHA1_BLOCK_SIZE;
 int res;
 if (partial + len < SHA1_BLOCK_SIZE) {
  sctx->count += len;
  memcpy(sctx->data + partial, data, len);
  return 0;
 }
 res = __sha1_update(sctx, data, len, partial);
 return res;
}
static int sha1_final(struct shash_desc *desc, u8 *out)
{
 struct SHA1_CTX *sctx = shash_desc_ctx(desc);
 unsigned int i, index, padlen;
 __be32 *dst = (__be32 *)out;
 __be64 bits;
 static const u8 padding[SHA1_BLOCK_SIZE] = { 0x80, };
 bits = cpu_to_be64(sctx->count << 3);
 index = sctx->count % SHA1_BLOCK_SIZE;
 padlen = (index < 56) ? (56 - index) : ((SHA1_BLOCK_SIZE+56) - index);
 if (padlen <= 56) {
  sctx->count += padlen;
  memcpy(sctx->data + index, padding, padlen);
 } else {
  __sha1_update(sctx, padding, padlen, index);
 }
 __sha1_update(sctx, (const u8 *)&bits, sizeof(bits), 56);
 for (i = 0; i < 5; i++)
  dst[i] = cpu_to_be32(((u32 *)sctx)[i]);
 memset(sctx, 0, sizeof(*sctx));
 return 0;
}
static int sha1_export(struct shash_desc *desc, void *out)
{
 struct SHA1_CTX *sctx = shash_desc_ctx(desc);
 memcpy(out, sctx, sizeof(*sctx));
 return 0;
}
static int sha1_import(struct shash_desc *desc, const void *in)
{
 struct SHA1_CTX *sctx = shash_desc_ctx(desc);
 memcpy(sctx, in, sizeof(*sctx));
 return 0;
}
static struct shash_alg alg = {
 .digestsize = SHA1_DIGEST_SIZE,
 .init = sha1_init,
 .update = sha1_update,
 .final = sha1_final,
 .export = sha1_export,
 .import = sha1_import,
 .descsize = sizeof(struct SHA1_CTX),
 .statesize = sizeof(struct SHA1_CTX),
 .base = {
  .cra_name = "sha1",
  .cra_driver_name= "sha1-asm",
  .cra_priority = 150,
  .cra_flags = CRYPTO_ALG_TYPE_SHASH,
  .cra_blocksize = SHA1_BLOCK_SIZE,
  .cra_module = THIS_MODULE,
 }
};
int sha1_mod_init(void)
{
 return crypto_register_shash(&alg);
}
void sha1_mod_fini(void)
{
 crypto_unregister_shash(&alg);
}
#if 0
module_init(sha1_mod_init);
module_exit(sha1_mod_fini);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm (ARM)");
MODULE_ALIAS("sha1");
MODULE_AUTHOR("David McCullough <ucdevel@gmail.com>");
#endif
