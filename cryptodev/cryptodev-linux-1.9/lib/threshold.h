/* Return the number of bytes after which the
 * kernel operation is more efficient to use.
 * If return value is -1, then kernel operation
 * cannot, or shouldn't be used, because it is always
 * slower.
 *
 * Running time ~= 1.2 seconds per call.
 */
int get_sha1_threshold();
int get_aes_sha1_threshold();
