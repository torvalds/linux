/*
 * work.c
 * Where all the hard work is done
 * (c) 2005 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#include "drill.h"
#include <ldns/ldns.h>

/**
 * Converts a hex string to binary data
 * len is the length of the string
 * buf is the buffer to store the result in
 * offset is the starting position in the result buffer
 *
 * This function returns the length of the result
 */
static size_t
hexstr2bin(char *hexstr, int len, uint8_t *buf, size_t offset, size_t buf_len)
{
	char c;
	int i; 
	uint8_t int8 = 0;
	int sec = 0;
	size_t bufpos = 0;
	
	if (len % 2 != 0) {
		return 0;
	}

	for (i=0; i<len; i++) {
		c = hexstr[i];

		/* case insensitive, skip spaces */
		if (c != ' ') {
			if (c >= '0' && c <= '9') {
				int8 += c & 0x0f;  
			} else if (c >= 'a' && c <= 'z') {
				int8 += (c & 0x0f) + 9;   
			} else if (c >= 'A' && c <= 'Z') {
				int8 += (c & 0x0f) + 9;   
			} else {
				return 0;
			}
			 
			if (sec == 0) {
				int8 = int8 << 4;
				sec = 1;
			} else {
				if (bufpos + offset + 1 <= buf_len) {
					buf[bufpos+offset] = int8;
					int8 = 0;
					sec = 0; 
					bufpos++;
				} else {
					error("Buffer too small in hexstr2bin");
				}
			}
		}
        }
        return bufpos;
}

static size_t
packetbuffromfile(char *filename, uint8_t *wire)
{
	FILE *fp = NULL;
	int c;
	
	/* stat hack
	 * 0 = normal
	 * 1 = comment (skip to end of line)
	 * 2 = unprintable character found, read binary data directly
	 */
	int state = 0;
	uint8_t *hexbuf = xmalloc(LDNS_MAX_PACKETLEN);
	int hexbufpos = 0;
	size_t wirelen;
	
	if (strncmp(filename, "-", 2) == 0) {
		fp = stdin;
	} else {
		fp = fopen(filename, "r");
	}
	if (fp == NULL) {
		perror("Unable to open file for reading");
		xfree(hexbuf);
		return 0;
	}

	/*verbose("Opened %s\n", filename);*/
	
	c = fgetc(fp);
	while (c != EOF && hexbufpos < LDNS_MAX_PACKETLEN) {
		if (state < 2 && !isascii(c)) {
			/*verbose("non ascii character found in file: (%d) switching to raw mode\n", c);*/
			state = 2;
		}
		switch (state) {
			case 0:
				if (	(c >= '0' && c <= '9') ||
					(c >= 'a' && c <= 'f') ||
					(c >= 'A' && c <= 'F') )
				{
					hexbuf[hexbufpos] = (uint8_t) c;
					hexbufpos++;
				} else if (c == ';') {
					state = 1;
				} else if (c == ' ' || c == '\t' || c == '\n') {
					/* skip whitespace */
				} 
				break;
			case 1:
				if (c == '\n' || c == EOF) {
					state = 0;
				}
				break;
			case 2:
				hexbuf[hexbufpos] = (uint8_t) c;
				hexbufpos++;
				break;
		}
		c = fgetc(fp);
	}

	if (c == EOF) {
		/*
		if (have_drill_opt && drill_opt->verbose) {
			verbose("END OF FILE REACHED\n");
			if (state < 2) {
				verbose("read:\n");
				verbose("%s\n", hexbuf);
			} else {
				verbose("Not printing wire because it contains non ascii data\n");
			}
		}
		*/
	}
	if (hexbufpos >= LDNS_MAX_PACKETLEN) {
		/*verbose("packet size reached\n");*/
	}
	
	/* lenient mode: length must be multiple of 2 */
	if (hexbufpos % 2 != 0) {
		hexbuf[hexbufpos] = (uint8_t) '0';
		hexbufpos++;
	}

	if (state < 2) {
		wirelen = hexstr2bin((char *) hexbuf,
						 hexbufpos,
						 wire,
						 0,
						 LDNS_MAX_PACKETLEN);
	} else {
		memcpy(wire, hexbuf, (size_t) hexbufpos);
		wirelen = (size_t) hexbufpos;
	}
	if (fp != stdin) {
		fclose(fp);
	}
	xfree(hexbuf);
	return wirelen;
}	

ldns_buffer *
read_hex_buffer(char *filename)
{
	uint8_t *wire;
	size_t wiresize;
	ldns_buffer *result_buffer = NULL;
	

	wire = xmalloc(LDNS_MAX_PACKETLEN);
	
	wiresize = packetbuffromfile(filename, wire);
	
	result_buffer = LDNS_MALLOC(ldns_buffer);
	ldns_buffer_new_frm_data(result_buffer, wire, wiresize);
	ldns_buffer_set_position(result_buffer, ldns_buffer_capacity(result_buffer));
	xfree(wire);

	return result_buffer;
}

ldns_pkt *
read_hex_pkt(char *filename)
{
	uint8_t *wire;
	size_t wiresize;
	
	ldns_pkt *pkt = NULL;
	
	ldns_status status = LDNS_STATUS_ERR;

	wire = xmalloc(LDNS_MAX_PACKETLEN);
	
	wiresize = packetbuffromfile(filename, wire);
	
	if (wiresize > 0) {
		status = ldns_wire2pkt(&pkt, wire, wiresize);
	}
	
	xfree(wire);
	
	if (status == LDNS_STATUS_OK) {
		return pkt;
	} else {
		fprintf(stderr, "Error parsing hex file: %s\n",
			   ldns_get_errorstr_by_id(status));
		return NULL;
	}
}

void
dump_hex(const ldns_pkt *pkt, const char *filename)
{
	uint8_t *wire = NULL;
	size_t size, i;
	FILE *fp;
	ldns_status status;
	
	fp = fopen(filename, "w");
	
	if (fp == NULL) {
		error("Unable to open %s for writing", filename);
		return;
	}
	
	status = ldns_pkt2wire(&wire, pkt, &size);
	
	if (status != LDNS_STATUS_OK) {
		error("Unable to convert packet: error code %u", status);
		LDNS_FREE(wire);
		fclose(fp);
		return;
	}
	
	fprintf(fp, "; 0");
	for (i = 1; i < 20; i++) {
		fprintf(fp, " %2u", (unsigned int) i);
	}
	fprintf(fp, "\n");
	fprintf(fp, ";--");
	for (i = 1; i < 20; i++) {
		fprintf(fp, " --");
	}
	fprintf(fp, "\n");
	for (i = 0; i < size; i++) {
		if (i % 20 == 0 && i > 0) {
			fprintf(fp, "\t;\t%4u-%4u\n", (unsigned int) i-19, (unsigned int) i);
		}
		fprintf(fp, " %02x", (unsigned int)wire[i]);
	}
	fprintf(fp, "\n");
	fclose(fp);
	LDNS_FREE(wire);
}
