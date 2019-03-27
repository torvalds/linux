/*
 * kmp_environment.h -- Handle environment varoiables OS-independently.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_ENVIRONMENT_H
#define KMP_ENVIRONMENT_H

#ifdef __cplusplus
extern "C" {
#endif

// Return a copy of the value of environment variable or NULL if the variable
// does not exist.
// *Note*: Returned pointed *must* be freed after use with __kmp_env_free().
char *__kmp_env_get(char const *name);
void __kmp_env_free(char const **value);

// Return 1 if the environment variable exists or 0 if does not exist.
int __kmp_env_exists(char const *name);

// Set the environment variable.
void __kmp_env_set(char const *name, char const *value, int overwrite);

// Unset (remove) environment variable.
void __kmp_env_unset(char const *name);

// -----------------------------------------------------------------------------
//  Working with environment blocks.

/* kmp_env_blk_t is read-only collection of environment variables (or
   environment-like). Usage:

kmp_env_blk_t block;
__kmp_env_blk_init( & block, NULL ); // Initialize block from process
                                        // environment.
// or
__kmp_env_blk_init( & block, "KMP_WARNING=1|KMP_AFFINITY=none" ); // from string
__kmp_env_blk_sort( & block ); // Optionally, sort list.
for ( i = 0; i < block.count; ++ i ) {
    // Process block.vars[ i ].name and block.vars[ i ].value...
}
__kmp_env_block_free( & block );
*/

struct __kmp_env_var {
  char *name;
  char *value;
};
typedef struct __kmp_env_var kmp_env_var_t;

struct __kmp_env_blk {
  char *bulk;
  kmp_env_var_t *vars;
  int count;
};
typedef struct __kmp_env_blk kmp_env_blk_t;

void __kmp_env_blk_init(kmp_env_blk_t *block, char const *bulk);
void __kmp_env_blk_free(kmp_env_blk_t *block);
void __kmp_env_blk_sort(kmp_env_blk_t *block);
char const *__kmp_env_blk_var(kmp_env_blk_t *block, char const *name);

#ifdef __cplusplus
}
#endif

#endif // KMP_ENVIRONMENT_H

// end of file //
