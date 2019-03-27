/*
 * parse.h 
 *
 * a Net::DNS like library for C
 * LibDNS Team @ NLnet Labs
 * (c) NLnet Labs, 2005-2006
 * See the file LICENSE for the license
 */

#ifndef LDNS_PARSE_H
#define LDNS_PARSE_H

struct sldns_buffer;

#ifdef __cplusplus
extern "C" {
#endif

#define LDNS_PARSE_SKIP_SPACE		"\f\n\r\v"
#define LDNS_PARSE_NORMAL		" \f\n\r\t\v"
#define LDNS_PARSE_NO_NL		" \t"
#define LDNS_MAX_LINELEN		10230
#define LDNS_MAX_KEYWORDLEN		32


/**
 * \file
 *
 * Contains some low-level parsing functions, mostly used in the _frm_str
 * family of functions.
 */
 
/**
 * different type of directives in zone files
 * We now deal with $TTL, $ORIGIN and $INCLUDE.
 * The latter is not implemented in ldns (yet)
 */
enum sldns_enum_directive
{
	LDNS_DIR_TTL,
	LDNS_DIR_ORIGIN,
	LDNS_DIR_INCLUDE
};
typedef enum sldns_enum_directive sldns_directive;

/** 
 * returns a token/char from the stream F.
 * This function deals with ( and ) in the stream,
 * and ignores them when encountered
 * \param[in] *f the file to read from
 * \param[out] *token the read token is put here
 * \param[in] *delim chars at which the parsing should stop
 * \param[in] *limit how much to read. If 0 the builtin maximum is used
 * \return 0 on error of EOF of the stream F.  Otherwise return the length of what is read
 */
ssize_t sldns_fget_token(FILE *f, char *token, const char *delim, size_t limit);

/** 
 * returns a token/char from the stream F.
 * This function deals with ( and ) in the stream,
 * and ignores when it finds them.
 * \param[in] *f the file to read from
 * \param[out] *token the token is put here
 * \param[in] *delim chars at which the parsing should stop
 * \param[in] *limit how much to read. If 0 use builtin maximum
 * \param[in] line_nr pointer to an integer containing the current line number (for debugging purposes)
 * \return 0 on error of EOF of F otherwise return the length of what is read
 */
ssize_t sldns_fget_token_l(FILE *f, char *token, const char *delim, size_t limit, int *line_nr);

/**
 * returns a token/char from the buffer b.
 * This function deals with ( and ) in the buffer,
 * and ignores when it finds them.
 * \param[in] *b the buffer to read from
 * \param[out] *token the token is put here
 * \param[in] *delim chars at which the parsing should stop
 * \param[in] *limit how much to read. If 0 the builtin maximum is used
 * \param[in] *par if you pass nonNULL, set to 0 on first call, the parenthesis
 * state is stored in it, for use on next call.  User must check it is back
 * to zero after last bget in string (for parse error).  If you pass NULL,
 * the entire parenthesized string is read in.
 * \param[in] skipw string with whitespace to skip before the start of the
 * token, like " ", or " \t", or NULL for none.  
 * \returns 0 on error of EOF of b. Otherwise return the length of what is read
 */
ssize_t sldns_bget_token_par(struct sldns_buffer *b, char *token, const char *delim, size_t limit, int* par, const char* skipw);

/**
 * returns a token/char from the buffer b.
 * This function deals with ( and ) in the buffer,
 * and ignores when it finds them.
 * \param[in] *b the buffer to read from
 * \param[out] *token the token is put here
 * \param[in] *delim chars at which the parsing should stop
 * \param[in] *limit how much to read. If 0 the builtin maximum is used
 * \returns 0 on error of EOF of b. Otherwise return the length of what is read
 */
ssize_t sldns_bget_token(struct sldns_buffer *b, char *token, const char *delim, size_t limit);

/*
 * searches for keyword and delim in a file. Gives everything back
 * after the keyword + k_del until we hit d_del
 * \param[in] f file pointer to read from
 * \param[in] keyword keyword to look for
 * \param[in] k_del keyword delimiter 
 * \param[out] data the data found 
 * \param[in] d_del the data delimiter
 * \param[in] data_limit maximum size the the data buffer
 * \return the number of character read
 */
ssize_t sldns_fget_keyword_data(FILE *f, const char *keyword, const char *k_del, char *data, const char *d_del, size_t data_limit);

/*
 * searches for keyword and delim. Gives everything back
 * after the keyword + k_del until we hit d_del
 * \param[in] f file pointer to read from
 * \param[in] keyword keyword to look for
 * \param[in] k_del keyword delimiter 
 * \param[out] data the data found 
 * \param[in] d_del the data delimiter
 * \param[in] data_limit maximum size the the data buffer
 * \param[in] line_nr pointer to an integer containing the current line number (for
debugging purposes)
 * \return the number of character read
 */
ssize_t sldns_fget_keyword_data_l(FILE *f, const char *keyword, const char *k_del, char *data, const char *d_del, size_t data_limit, int *line_nr);

/*
 * searches for keyword and delim in a buffer. Gives everything back
 * after the keyword + k_del until we hit d_del
 * \param[in] b buffer pointer to read from
 * \param[in] keyword keyword to look for
 * \param[in] k_del keyword delimiter 
 * \param[out] data the data found 
 * \param[in] d_del the data delimiter
 * \param[in] data_limit maximum size the the data buffer
 * \return the number of character read
 */
ssize_t sldns_bget_keyword_data(struct sldns_buffer *b, const char *keyword, const char *k_del, char *data, const char *d_del, size_t data_limit);

/**
 * returns the next character from a buffer. Advances the position pointer with 1.
 * When end of buffer is reached returns EOF. This is the buffer's equivalent
 * for getc().
 * \param[in] *buffer buffer to read from
 * \return EOF on failure otherwise return the character
 */
int sldns_bgetc(struct sldns_buffer *buffer);

/**
 * skips all of the characters in the given string in the buffer, moving
 * the position to the first character that is not in *s.
 * \param[in] *buffer buffer to use
 * \param[in] *s characters to skip
 * \return void
 */
void sldns_bskipcs(struct sldns_buffer *buffer, const char *s);

/**
 * skips all of the characters in the given string in the fp, moving
 * the position to the first character that is not in *s.
 * \param[in] *fp file to use
 * \param[in] *s characters to skip
 * \return void
 */
void sldns_fskipcs(FILE *fp, const char *s);


/**
 * skips all of the characters in the given string in the fp, moving
 * the position to the first character that is not in *s.
 * \param[in] *fp file to use
 * \param[in] *s characters to skip
 * \param[in] line_nr pointer to an integer containing the current line number (for debugging purposes)
 * \return void
 */
void sldns_fskipcs_l(FILE *fp, const char *s, int *line_nr);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_PARSE_H */
