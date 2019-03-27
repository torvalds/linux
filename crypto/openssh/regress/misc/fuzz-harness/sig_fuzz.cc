// cc_fuzz_target test for public key parsing.

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern "C" {

#include "includes.h"
#include "sshkey.h"
#include "ssherr.h"

static struct sshkey *generate_or_die(int type, unsigned bits) {
  int r;
  struct sshkey *ret;
  if ((r = sshkey_generate(type, bits, &ret)) != 0) {
    fprintf(stderr, "generate(%d, %u): %s", type, bits, ssh_err(r));
    abort();
  }
  return ret;
}

int LLVMFuzzerTestOneInput(const uint8_t* sig, size_t slen)
{
#ifdef WITH_OPENSSL
  static struct sshkey *rsa = generate_or_die(KEY_RSA, 2048);
  static struct sshkey *dsa = generate_or_die(KEY_DSA, 1024);
  static struct sshkey *ecdsa256 = generate_or_die(KEY_ECDSA, 256);
  static struct sshkey *ecdsa384 = generate_or_die(KEY_ECDSA, 384);
  static struct sshkey *ecdsa521 = generate_or_die(KEY_ECDSA, 521);
#endif
  static struct sshkey *ed25519 = generate_or_die(KEY_ED25519, 0);
  static const char *data = "If everyone started announcing his nose had "
      "run away, I don’t know how it would all end";
  static const size_t dlen = strlen(data);

#ifdef WITH_OPENSSL
  sshkey_verify(rsa, sig, slen, (const u_char *)data, dlen, NULL, 0);
  sshkey_verify(dsa, sig, slen, (const u_char *)data, dlen, NULL, 0);
  sshkey_verify(ecdsa256, sig, slen, (const u_char *)data, dlen, NULL, 0);
  sshkey_verify(ecdsa384, sig, slen, (const u_char *)data, dlen, NULL, 0);
  sshkey_verify(ecdsa521, sig, slen, (const u_char *)data, dlen, NULL, 0);
#endif
  sshkey_verify(ed25519, sig, slen, (const u_char *)data, dlen, NULL, 0);
  return 0;
}

} // extern
