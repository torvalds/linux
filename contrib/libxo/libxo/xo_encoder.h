/*
 * Copyright (c) 2015, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, August 2015
 */

/*
 * NOTE WELL: This file is needed to software that implements an
 * external encoder for libxo that allows libxo data to be encoded in
 * new and bizarre formats.  General libxo code should _never_
 * include this header file.
 */

#ifndef XO_ENCODER_H
#define XO_ENCODER_H

/*
 * Expose libxo's memory allocation functions
 */
extern xo_realloc_func_t xo_realloc;
extern xo_free_func_t xo_free;

typedef unsigned xo_encoder_op_t;

/* Encoder operations; names are in xo_encoder.c:xo_encoder_op_name() */
#define XO_OP_UNKNOWN		0
#define XO_OP_CREATE		1 /* Called when the handle is init'd */
#define XO_OP_OPEN_CONTAINER	2
#define XO_OP_CLOSE_CONTAINER	3
#define XO_OP_OPEN_LIST		4
#define XO_OP_CLOSE_LIST	5
#define XO_OP_OPEN_LEAF_LIST	6
#define XO_OP_CLOSE_LEAF_LIST	7
#define XO_OP_OPEN_INSTANCE	8
#define XO_OP_CLOSE_INSTANCE	9
#define XO_OP_STRING		10 /* Quoted UTF-8 string */
#define XO_OP_CONTENT		11 /* Other content */
#define XO_OP_FINISH		12 /* Finish any pending output */
#define XO_OP_FLUSH		13 /* Flush any buffered output */
#define XO_OP_DESTROY		14 /* Clean up function */
#define XO_OP_ATTRIBUTE		15 /* Attribute name/value */
#define XO_OP_VERSION		16 /* Version string */

#define XO_ENCODER_HANDLER_ARGS					\
	xo_handle_t *xop __attribute__ ((__unused__)),		\
	xo_encoder_op_t op __attribute__ ((__unused__)),	\
	const char *name __attribute__ ((__unused__)),		\
        const char *value __attribute__ ((__unused__)),		\
	void *private __attribute__ ((__unused__)),		\
	xo_xof_flags_t flags __attribute__ ((__unused__))

typedef int (*xo_encoder_func_t)(XO_ENCODER_HANDLER_ARGS);

typedef struct xo_encoder_init_args_s {
    unsigned xei_version;	   /* Current version */
    xo_encoder_func_t xei_handler; /* Encoding handler */
} xo_encoder_init_args_t;

#define XO_ENCODER_VERSION	1 /* Current version */

#define XO_ENCODER_INIT_ARGS \
    xo_encoder_init_args_t *arg __attribute__ ((__unused__))

typedef int (*xo_encoder_init_func_t)(XO_ENCODER_INIT_ARGS);
/*
 * Each encoder library must define a function named xo_encoder_init
 * that takes the arguments defined in XO_ENCODER_INIT_ARGS.  It
 * should return zero for success.
 */
#define XO_ENCODER_INIT_NAME_TOKEN xo_encoder_library_init
#define XO_STRINGIFY(_x) #_x
#define XO_STRINGIFY2(_x) XO_STRINGIFY(_x)
#define XO_ENCODER_INIT_NAME XO_STRINGIFY2(XO_ENCODER_INIT_NAME_TOKEN)
extern int XO_ENCODER_INIT_NAME_TOKEN (XO_ENCODER_INIT_ARGS);

void
xo_encoder_register (const char *name, xo_encoder_func_t func);

void
xo_encoder_unregister (const char *name);

void *
xo_get_private (xo_handle_t *xop);

void
xo_encoder_path_add (const char *path);

void
xo_set_private (xo_handle_t *xop, void *opaque);

xo_encoder_func_t
xo_get_encoder (xo_handle_t *xop);

void
xo_set_encoder (xo_handle_t *xop, xo_encoder_func_t encoder);

int
xo_encoder_init (xo_handle_t *xop, const char *name);

xo_handle_t *
xo_encoder_create (const char *name, xo_xof_flags_t flags);

int
xo_encoder_handle (xo_handle_t *xop, xo_encoder_op_t op,
		   const char *name, const char *value, xo_xof_flags_t flags);

void
xo_encoders_clean (void);

const char *
xo_encoder_op_name (xo_encoder_op_t op);

#endif /* XO_ENCODER_H */
