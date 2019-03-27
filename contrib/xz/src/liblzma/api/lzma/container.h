/**
 * \file        lzma/container.h
 * \brief       File formats
 */

/*
 * Author: Lasse Collin
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 *
 * See ../lzma.h for information about liblzma as a whole.
 */

#ifndef LZMA_H_INTERNAL
#	error Never include this file directly. Use <lzma.h> instead.
#endif


/************
 * Encoding *
 ************/

/**
 * \brief       Default compression preset
 *
 * It's not straightforward to recommend a default preset, because in some
 * cases keeping the resource usage relatively low is more important that
 * getting the maximum compression ratio.
 */
#define LZMA_PRESET_DEFAULT     UINT32_C(6)


/**
 * \brief       Mask for preset level
 *
 * This is useful only if you need to extract the level from the preset
 * variable. That should be rare.
 */
#define LZMA_PRESET_LEVEL_MASK  UINT32_C(0x1F)


/*
 * Preset flags
 *
 * Currently only one flag is defined.
 */

/**
 * \brief       Extreme compression preset
 *
 * This flag modifies the preset to make the encoding significantly slower
 * while improving the compression ratio only marginally. This is useful
 * when you don't mind wasting time to get as small result as possible.
 *
 * This flag doesn't affect the memory usage requirements of the decoder (at
 * least not significantly). The memory usage of the encoder may be increased
 * a little but only at the lowest preset levels (0-3).
 */
#define LZMA_PRESET_EXTREME       (UINT32_C(1) << 31)


/**
 * \brief       Multithreading options
 */
typedef struct {
	/**
	 * \brief       Flags
	 *
	 * Set this to zero if no flags are wanted.
	 *
	 * No flags are currently supported.
	 */
	uint32_t flags;

	/**
	 * \brief       Number of worker threads to use
	 */
	uint32_t threads;

	/**
	 * \brief       Maximum uncompressed size of a Block
	 *
	 * The encoder will start a new .xz Block every block_size bytes.
	 * Using LZMA_FULL_FLUSH or LZMA_FULL_BARRIER with lzma_code()
	 * the caller may tell liblzma to start a new Block earlier.
	 *
	 * With LZMA2, a recommended block size is 2-4 times the LZMA2
	 * dictionary size. With very small dictionaries, it is recommended
	 * to use at least 1 MiB block size for good compression ratio, even
	 * if this is more than four times the dictionary size. Note that
	 * these are only recommendations for typical use cases; feel free
	 * to use other values. Just keep in mind that using a block size
	 * less than the LZMA2 dictionary size is waste of RAM.
	 *
	 * Set this to 0 to let liblzma choose the block size depending
	 * on the compression options. For LZMA2 it will be 3*dict_size
	 * or 1 MiB, whichever is more.
	 *
	 * For each thread, about 3 * block_size bytes of memory will be
	 * allocated. This may change in later liblzma versions. If so,
	 * the memory usage will probably be reduced, not increased.
	 */
	uint64_t block_size;

	/**
	 * \brief       Timeout to allow lzma_code() to return early
	 *
	 * Multithreading can make liblzma to consume input and produce
	 * output in a very bursty way: it may first read a lot of input
	 * to fill internal buffers, then no input or output occurs for
	 * a while.
	 *
	 * In single-threaded mode, lzma_code() won't return until it has
	 * either consumed all the input or filled the output buffer. If
	 * this is done in multithreaded mode, it may cause a call
	 * lzma_code() to take even tens of seconds, which isn't acceptable
	 * in all applications.
	 *
	 * To avoid very long blocking times in lzma_code(), a timeout
	 * (in milliseconds) may be set here. If lzma_code() would block
	 * longer than this number of milliseconds, it will return with
	 * LZMA_OK. Reasonable values are 100 ms or more. The xz command
	 * line tool uses 300 ms.
	 *
	 * If long blocking times are fine for you, set timeout to a special
	 * value of 0, which will disable the timeout mechanism and will make
	 * lzma_code() block until all the input is consumed or the output
	 * buffer has been filled.
	 *
	 * \note        Even with a timeout, lzma_code() might sometimes take
	 *              somewhat long time to return. No timing guarantees
	 *              are made.
	 */
	uint32_t timeout;

	/**
	 * \brief       Compression preset (level and possible flags)
	 *
	 * The preset is set just like with lzma_easy_encoder().
	 * The preset is ignored if filters below is non-NULL.
	 */
	uint32_t preset;

	/**
	 * \brief       Filter chain (alternative to a preset)
	 *
	 * If this is NULL, the preset above is used. Otherwise the preset
	 * is ignored and the filter chain specified here is used.
	 */
	const lzma_filter *filters;

	/**
	 * \brief       Integrity check type
	 *
	 * See check.h for available checks. The xz command line tool
	 * defaults to LZMA_CHECK_CRC64, which is a good choice if you
	 * are unsure.
	 */
	lzma_check check;

	/*
	 * Reserved space to allow possible future extensions without
	 * breaking the ABI. You should not touch these, because the names
	 * of these variables may change. These are and will never be used
	 * with the currently supported options, so it is safe to leave these
	 * uninitialized.
	 */
	lzma_reserved_enum reserved_enum1;
	lzma_reserved_enum reserved_enum2;
	lzma_reserved_enum reserved_enum3;
	uint32_t reserved_int1;
	uint32_t reserved_int2;
	uint32_t reserved_int3;
	uint32_t reserved_int4;
	uint64_t reserved_int5;
	uint64_t reserved_int6;
	uint64_t reserved_int7;
	uint64_t reserved_int8;
	void *reserved_ptr1;
	void *reserved_ptr2;
	void *reserved_ptr3;
	void *reserved_ptr4;

} lzma_mt;


/**
 * \brief       Calculate approximate memory usage of easy encoder
 *
 * This function is a wrapper for lzma_raw_encoder_memusage().
 *
 * \param       preset  Compression preset (level and possible flags)
 *
 * \return      Number of bytes of memory required for the given
 *              preset when encoding. If an error occurs, for example
 *              due to unsupported preset, UINT64_MAX is returned.
 */
extern LZMA_API(uint64_t) lzma_easy_encoder_memusage(uint32_t preset)
		lzma_nothrow lzma_attr_pure;


/**
 * \brief       Calculate approximate decoder memory usage of a preset
 *
 * This function is a wrapper for lzma_raw_decoder_memusage().
 *
 * \param       preset  Compression preset (level and possible flags)
 *
 * \return      Number of bytes of memory required to decompress a file
 *              that was compressed using the given preset. If an error
 *              occurs, for example due to unsupported preset, UINT64_MAX
 *              is returned.
 */
extern LZMA_API(uint64_t) lzma_easy_decoder_memusage(uint32_t preset)
		lzma_nothrow lzma_attr_pure;


/**
 * \brief       Initialize .xz Stream encoder using a preset number
 *
 * This function is intended for those who just want to use the basic features
 * if liblzma (that is, most developers out there).
 *
 * \param       strm    Pointer to lzma_stream that is at least initialized
 *                      with LZMA_STREAM_INIT.
 * \param       preset  Compression preset to use. A preset consist of level
 *                      number and zero or more flags. Usually flags aren't
 *                      used, so preset is simply a number [0, 9] which match
 *                      the options -0 ... -9 of the xz command line tool.
 *                      Additional flags can be be set using bitwise-or with
 *                      the preset level number, e.g. 6 | LZMA_PRESET_EXTREME.
 * \param       check   Integrity check type to use. See check.h for available
 *                      checks. The xz command line tool defaults to
 *                      LZMA_CHECK_CRC64, which is a good choice if you are
 *                      unsure. LZMA_CHECK_CRC32 is good too as long as the
 *                      uncompressed file is not many gigabytes.
 *
 * \return      - LZMA_OK: Initialization succeeded. Use lzma_code() to
 *                encode your data.
 *              - LZMA_MEM_ERROR: Memory allocation failed.
 *              - LZMA_OPTIONS_ERROR: The given compression preset is not
 *                supported by this build of liblzma.
 *              - LZMA_UNSUPPORTED_CHECK: The given check type is not
 *                supported by this liblzma build.
 *              - LZMA_PROG_ERROR: One or more of the parameters have values
 *                that will never be valid. For example, strm == NULL.
 *
 * If initialization fails (return value is not LZMA_OK), all the memory
 * allocated for *strm by liblzma is always freed. Thus, there is no need
 * to call lzma_end() after failed initialization.
 *
 * If initialization succeeds, use lzma_code() to do the actual encoding.
 * Valid values for `action' (the second argument of lzma_code()) are
 * LZMA_RUN, LZMA_SYNC_FLUSH, LZMA_FULL_FLUSH, and LZMA_FINISH. In future,
 * there may be compression levels or flags that don't support LZMA_SYNC_FLUSH.
 */
extern LZMA_API(lzma_ret) lzma_easy_encoder(
		lzma_stream *strm, uint32_t preset, lzma_check check)
		lzma_nothrow lzma_attr_warn_unused_result;


/**
 * \brief       Single-call .xz Stream encoding using a preset number
 *
 * The maximum required output buffer size can be calculated with
 * lzma_stream_buffer_bound().
 *
 * \param       preset      Compression preset to use. See the description
 *                          in lzma_easy_encoder().
 * \param       check       Type of the integrity check to calculate from
 *                          uncompressed data.
 * \param       allocator   lzma_allocator for custom allocator functions.
 *                          Set to NULL to use malloc() and free().
 * \param       in          Beginning of the input buffer
 * \param       in_size     Size of the input buffer
 * \param       out         Beginning of the output buffer
 * \param       out_pos     The next byte will be written to out[*out_pos].
 *                          *out_pos is updated only if encoding succeeds.
 * \param       out_size    Size of the out buffer; the first byte into
 *                          which no data is written to is out[out_size].
 *
 * \return      - LZMA_OK: Encoding was successful.
 *              - LZMA_BUF_ERROR: Not enough output buffer space.
 *              - LZMA_UNSUPPORTED_CHECK
 *              - LZMA_OPTIONS_ERROR
 *              - LZMA_MEM_ERROR
 *              - LZMA_DATA_ERROR
 *              - LZMA_PROG_ERROR
 */
extern LZMA_API(lzma_ret) lzma_easy_buffer_encode(
		uint32_t preset, lzma_check check,
		const lzma_allocator *allocator,
		const uint8_t *in, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size) lzma_nothrow;


/**
 * \brief       Initialize .xz Stream encoder using a custom filter chain
 *
 * \param       strm    Pointer to properly prepared lzma_stream
 * \param       filters Array of filters. This must be terminated with
 *                      filters[n].id = LZMA_VLI_UNKNOWN. See filter.h for
 *                      more information.
 * \param       check   Type of the integrity check to calculate from
 *                      uncompressed data.
 *
 * \return      - LZMA_OK: Initialization was successful.
 *              - LZMA_MEM_ERROR
 *              - LZMA_UNSUPPORTED_CHECK
 *              - LZMA_OPTIONS_ERROR
 *              - LZMA_PROG_ERROR
 */
extern LZMA_API(lzma_ret) lzma_stream_encoder(lzma_stream *strm,
		const lzma_filter *filters, lzma_check check)
		lzma_nothrow lzma_attr_warn_unused_result;


/**
 * \brief       Calculate approximate memory usage of multithreaded .xz encoder
 *
 * Since doing the encoding in threaded mode doesn't affect the memory
 * requirements of single-threaded decompressor, you can use
 * lzma_easy_decoder_memusage(options->preset) or
 * lzma_raw_decoder_memusage(options->filters) to calculate
 * the decompressor memory requirements.
 *
 * \param       options Compression options
 *
 * \return      Number of bytes of memory required for encoding with the
 *              given options. If an error occurs, for example due to
 *              unsupported preset or filter chain, UINT64_MAX is returned.
 */
extern LZMA_API(uint64_t) lzma_stream_encoder_mt_memusage(
		const lzma_mt *options) lzma_nothrow lzma_attr_pure;


/**
 * \brief       Initialize multithreaded .xz Stream encoder
 *
 * This provides the functionality of lzma_easy_encoder() and
 * lzma_stream_encoder() as a single function for multithreaded use.
 *
 * The supported actions for lzma_code() are LZMA_RUN, LZMA_FULL_FLUSH,
 * LZMA_FULL_BARRIER, and LZMA_FINISH. Support for LZMA_SYNC_FLUSH might be
 * added in the future.
 *
 * \param       strm    Pointer to properly prepared lzma_stream
 * \param       options Pointer to multithreaded compression options
 *
 * \return      - LZMA_OK
 *              - LZMA_MEM_ERROR
 *              - LZMA_UNSUPPORTED_CHECK
 *              - LZMA_OPTIONS_ERROR
 *              - LZMA_PROG_ERROR
 */
extern LZMA_API(lzma_ret) lzma_stream_encoder_mt(
		lzma_stream *strm, const lzma_mt *options)
		lzma_nothrow lzma_attr_warn_unused_result;


/**
 * \brief       Initialize .lzma encoder (legacy file format)
 *
 * The .lzma format is sometimes called the LZMA_Alone format, which is the
 * reason for the name of this function. The .lzma format supports only the
 * LZMA1 filter. There is no support for integrity checks like CRC32.
 *
 * Use this function if and only if you need to create files readable by
 * legacy LZMA tools such as LZMA Utils 4.32.x. Moving to the .xz format
 * is strongly recommended.
 *
 * The valid action values for lzma_code() are LZMA_RUN and LZMA_FINISH.
 * No kind of flushing is supported, because the file format doesn't make
 * it possible.
 *
 * \return      - LZMA_OK
 *              - LZMA_MEM_ERROR
 *              - LZMA_OPTIONS_ERROR
 *              - LZMA_PROG_ERROR
 */
extern LZMA_API(lzma_ret) lzma_alone_encoder(
		lzma_stream *strm, const lzma_options_lzma *options)
		lzma_nothrow lzma_attr_warn_unused_result;


/**
 * \brief       Calculate output buffer size for single-call Stream encoder
 *
 * When trying to compress uncompressible data, the encoded size will be
 * slightly bigger than the input data. This function calculates how much
 * output buffer space is required to be sure that lzma_stream_buffer_encode()
 * doesn't return LZMA_BUF_ERROR.
 *
 * The calculated value is not exact, but it is guaranteed to be big enough.
 * The actual maximum output space required may be slightly smaller (up to
 * about 100 bytes). This should not be a problem in practice.
 *
 * If the calculated maximum size doesn't fit into size_t or would make the
 * Stream grow past LZMA_VLI_MAX (which should never happen in practice),
 * zero is returned to indicate the error.
 *
 * \note        The limit calculated by this function applies only to
 *              single-call encoding. Multi-call encoding may (and probably
 *              will) have larger maximum expansion when encoding
 *              uncompressible data. Currently there is no function to
 *              calculate the maximum expansion of multi-call encoding.
 */
extern LZMA_API(size_t) lzma_stream_buffer_bound(size_t uncompressed_size)
		lzma_nothrow;


/**
 * \brief       Single-call .xz Stream encoder
 *
 * \param       filters     Array of filters. This must be terminated with
 *                          filters[n].id = LZMA_VLI_UNKNOWN. See filter.h
 *                          for more information.
 * \param       check       Type of the integrity check to calculate from
 *                          uncompressed data.
 * \param       allocator   lzma_allocator for custom allocator functions.
 *                          Set to NULL to use malloc() and free().
 * \param       in          Beginning of the input buffer
 * \param       in_size     Size of the input buffer
 * \param       out         Beginning of the output buffer
 * \param       out_pos     The next byte will be written to out[*out_pos].
 *                          *out_pos is updated only if encoding succeeds.
 * \param       out_size    Size of the out buffer; the first byte into
 *                          which no data is written to is out[out_size].
 *
 * \return      - LZMA_OK: Encoding was successful.
 *              - LZMA_BUF_ERROR: Not enough output buffer space.
 *              - LZMA_UNSUPPORTED_CHECK
 *              - LZMA_OPTIONS_ERROR
 *              - LZMA_MEM_ERROR
 *              - LZMA_DATA_ERROR
 *              - LZMA_PROG_ERROR
 */
extern LZMA_API(lzma_ret) lzma_stream_buffer_encode(
		lzma_filter *filters, lzma_check check,
		const lzma_allocator *allocator,
		const uint8_t *in, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size)
		lzma_nothrow lzma_attr_warn_unused_result;


/************
 * Decoding *
 ************/

/**
 * This flag makes lzma_code() return LZMA_NO_CHECK if the input stream
 * being decoded has no integrity check. Note that when used with
 * lzma_auto_decoder(), all .lzma files will trigger LZMA_NO_CHECK
 * if LZMA_TELL_NO_CHECK is used.
 */
#define LZMA_TELL_NO_CHECK              UINT32_C(0x01)


/**
 * This flag makes lzma_code() return LZMA_UNSUPPORTED_CHECK if the input
 * stream has an integrity check, but the type of the integrity check is not
 * supported by this liblzma version or build. Such files can still be
 * decoded, but the integrity check cannot be verified.
 */
#define LZMA_TELL_UNSUPPORTED_CHECK     UINT32_C(0x02)


/**
 * This flag makes lzma_code() return LZMA_GET_CHECK as soon as the type
 * of the integrity check is known. The type can then be got with
 * lzma_get_check().
 */
#define LZMA_TELL_ANY_CHECK             UINT32_C(0x04)


/**
 * This flag makes lzma_code() not calculate and verify the integrity check
 * of the compressed data in .xz files. This means that invalid integrity
 * check values won't be detected and LZMA_DATA_ERROR won't be returned in
 * such cases.
 *
 * This flag only affects the checks of the compressed data itself; the CRC32
 * values in the .xz headers will still be verified normally.
 *
 * Don't use this flag unless you know what you are doing. Possible reasons
 * to use this flag:
 *
 *   - Trying to recover data from a corrupt .xz file.
 *
 *   - Speeding up decompression, which matters mostly with SHA-256
 *     or with files that have compressed extremely well. It's recommended
 *     to not use this flag for this purpose unless the file integrity is
 *     verified externally in some other way.
 *
 * Support for this flag was added in liblzma 5.1.4beta.
 */
#define LZMA_IGNORE_CHECK               UINT32_C(0x10)


/**
 * This flag enables decoding of concatenated files with file formats that
 * allow concatenating compressed files as is. From the formats currently
 * supported by liblzma, only the .xz format allows concatenated files.
 * Concatenated files are not allowed with the legacy .lzma format.
 *
 * This flag also affects the usage of the `action' argument for lzma_code().
 * When LZMA_CONCATENATED is used, lzma_code() won't return LZMA_STREAM_END
 * unless LZMA_FINISH is used as `action'. Thus, the application has to set
 * LZMA_FINISH in the same way as it does when encoding.
 *
 * If LZMA_CONCATENATED is not used, the decoders still accept LZMA_FINISH
 * as `action' for lzma_code(), but the usage of LZMA_FINISH isn't required.
 */
#define LZMA_CONCATENATED               UINT32_C(0x08)


/**
 * \brief       Initialize .xz Stream decoder
 *
 * \param       strm        Pointer to properly prepared lzma_stream
 * \param       memlimit    Memory usage limit as bytes. Use UINT64_MAX
 *                          to effectively disable the limiter. liblzma
 *                          5.2.3 and earlier don't allow 0 here and return
 *                          LZMA_PROG_ERROR; later versions treat 0 as if 1
 *                          had been specified.
 * \param       flags       Bitwise-or of zero or more of the decoder flags:
 *                          LZMA_TELL_NO_CHECK, LZMA_TELL_UNSUPPORTED_CHECK,
 *                          LZMA_TELL_ANY_CHECK, LZMA_CONCATENATED
 *
 * \return      - LZMA_OK: Initialization was successful.
 *              - LZMA_MEM_ERROR: Cannot allocate memory.
 *              - LZMA_OPTIONS_ERROR: Unsupported flags
 *              - LZMA_PROG_ERROR
 */
extern LZMA_API(lzma_ret) lzma_stream_decoder(
		lzma_stream *strm, uint64_t memlimit, uint32_t flags)
		lzma_nothrow lzma_attr_warn_unused_result;


/**
 * \brief       Decode .xz Streams and .lzma files with autodetection
 *
 * This decoder autodetects between the .xz and .lzma file formats, and
 * calls lzma_stream_decoder() or lzma_alone_decoder() once the type
 * of the input file has been detected.
 *
 * \param       strm        Pointer to properly prepared lzma_stream
 * \param       memlimit    Memory usage limit as bytes. Use UINT64_MAX
 *                          to effectively disable the limiter. liblzma
 *                          5.2.3 and earlier don't allow 0 here and return
 *                          LZMA_PROG_ERROR; later versions treat 0 as if 1
 *                          had been specified.
 * \param       flags       Bitwise-or of flags, or zero for no flags.
 *
 * \return      - LZMA_OK: Initialization was successful.
 *              - LZMA_MEM_ERROR: Cannot allocate memory.
 *              - LZMA_OPTIONS_ERROR: Unsupported flags
 *              - LZMA_PROG_ERROR
 */
extern LZMA_API(lzma_ret) lzma_auto_decoder(
		lzma_stream *strm, uint64_t memlimit, uint32_t flags)
		lzma_nothrow lzma_attr_warn_unused_result;


/**
 * \brief       Initialize .lzma decoder (legacy file format)
 *
 * \param       strm        Pointer to properly prepared lzma_stream
 * \param       memlimit    Memory usage limit as bytes. Use UINT64_MAX
 *                          to effectively disable the limiter. liblzma
 *                          5.2.3 and earlier don't allow 0 here and return
 *                          LZMA_PROG_ERROR; later versions treat 0 as if 1
 *                          had been specified.
 *
 * Valid `action' arguments to lzma_code() are LZMA_RUN and LZMA_FINISH.
 * There is no need to use LZMA_FINISH, but it's allowed because it may
 * simplify certain types of applications.
 *
 * \return      - LZMA_OK
 *              - LZMA_MEM_ERROR
 *              - LZMA_PROG_ERROR
 */
extern LZMA_API(lzma_ret) lzma_alone_decoder(
		lzma_stream *strm, uint64_t memlimit)
		lzma_nothrow lzma_attr_warn_unused_result;


/**
 * \brief       Single-call .xz Stream decoder
 *
 * \param       memlimit    Pointer to how much memory the decoder is allowed
 *                          to allocate. The value pointed by this pointer is
 *                          modified if and only if LZMA_MEMLIMIT_ERROR is
 *                          returned.
 * \param       flags       Bitwise-or of zero or more of the decoder flags:
 *                          LZMA_TELL_NO_CHECK, LZMA_TELL_UNSUPPORTED_CHECK,
 *                          LZMA_CONCATENATED. Note that LZMA_TELL_ANY_CHECK
 *                          is not allowed and will return LZMA_PROG_ERROR.
 * \param       allocator   lzma_allocator for custom allocator functions.
 *                          Set to NULL to use malloc() and free().
 * \param       in          Beginning of the input buffer
 * \param       in_pos      The next byte will be read from in[*in_pos].
 *                          *in_pos is updated only if decoding succeeds.
 * \param       in_size     Size of the input buffer; the first byte that
 *                          won't be read is in[in_size].
 * \param       out         Beginning of the output buffer
 * \param       out_pos     The next byte will be written to out[*out_pos].
 *                          *out_pos is updated only if decoding succeeds.
 * \param       out_size    Size of the out buffer; the first byte into
 *                          which no data is written to is out[out_size].
 *
 * \return      - LZMA_OK: Decoding was successful.
 *              - LZMA_FORMAT_ERROR
 *              - LZMA_OPTIONS_ERROR
 *              - LZMA_DATA_ERROR
 *              - LZMA_NO_CHECK: This can be returned only if using
 *                the LZMA_TELL_NO_CHECK flag.
 *              - LZMA_UNSUPPORTED_CHECK: This can be returned only if using
 *                the LZMA_TELL_UNSUPPORTED_CHECK flag.
 *              - LZMA_MEM_ERROR
 *              - LZMA_MEMLIMIT_ERROR: Memory usage limit was reached.
 *                The minimum required memlimit value was stored to *memlimit.
 *              - LZMA_BUF_ERROR: Output buffer was too small.
 *              - LZMA_PROG_ERROR
 */
extern LZMA_API(lzma_ret) lzma_stream_buffer_decode(
		uint64_t *memlimit, uint32_t flags,
		const lzma_allocator *allocator,
		const uint8_t *in, size_t *in_pos, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size)
		lzma_nothrow lzma_attr_warn_unused_result;
