/* File I/O for GNU DIFF.

   Copyright (C) 1988, 1989, 1992, 1993, 1994, 1995, 1998, 2001, 2002,
   2004 Free Software Foundation, Inc.

   This file is part of GNU DIFF.

   GNU DIFF is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GNU DIFF is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "diff.h"
#include <cmpbuf.h>
#include <file-type.h>
#include <setmode.h>
#include <xalloc.h>

/* Rotate an unsigned value to the left.  */
#define ROL(v, n) ((v) << (n) | (v) >> (sizeof (v) * CHAR_BIT - (n)))

/* Given a hash value and a new character, return a new hash value.  */
#define HASH(h, c) ((c) + ROL (h, 7))

/* The type of a hash value.  */
typedef size_t hash_value;
verify (hash_value_is_unsigned, ! TYPE_SIGNED (hash_value));

/* Lines are put into equivalence classes of lines that match in lines_differ.
   Each equivalence class is represented by one of these structures,
   but only while the classes are being computed.
   Afterward, each class is represented by a number.  */
struct equivclass
{
  lin next;		/* Next item in this bucket.  */
  hash_value hash;	/* Hash of lines in this class.  */
  char const *line;	/* A line that fits this class.  */
  size_t length;	/* That line's length, not counting its newline.  */
};

/* Hash-table: array of buckets, each being a chain of equivalence classes.
   buckets[-1] is reserved for incomplete lines.  */
static lin *buckets;

/* Number of buckets in the hash table array, not counting buckets[-1].  */
static size_t nbuckets;

/* Array in which the equivalence classes are allocated.
   The bucket-chains go through the elements in this array.
   The number of an equivalence class is its index in this array.  */
static struct equivclass *equivs;

/* Index of first free element in the array `equivs'.  */
static lin equivs_index;

/* Number of elements allocated in the array `equivs'.  */
static lin equivs_alloc;

/* Read a block of data into a file buffer, checking for EOF and error.  */

void
file_block_read (struct file_data *current, size_t size)
{
  if (size && ! current->eof)
    {
      size_t s = block_read (current->desc,
			     FILE_BUFFER (current) + current->buffered, size);
      if (s == SIZE_MAX)
	pfatal_with_name (current->name);
      current->buffered += s;
      current->eof = s < size;
    }
}

/* Check for binary files and compare them for exact identity.  */

/* Return 1 if BUF contains a non text character.
   SIZE is the number of characters in BUF.  */

#define binary_file_p(buf, size) (memchr (buf, 0, size) != 0)

/* Get ready to read the current file.
   Return nonzero if SKIP_TEST is zero,
   and if it appears to be a binary file.  */

static bool
sip (struct file_data *current, bool skip_test)
{
  /* If we have a nonexistent file at this stage, treat it as empty.  */
  if (current->desc < 0)
    {
      /* Leave room for a sentinel.  */
      current->bufsize = sizeof (word);
      current->buffer = xmalloc (current->bufsize);
    }
  else
    {
      current->bufsize = buffer_lcm (sizeof (word),
				     STAT_BLOCKSIZE (current->stat),
				     PTRDIFF_MAX - 2 * sizeof (word));
      current->buffer = xmalloc (current->bufsize);

      if (! skip_test)
	{
	  /* Check first part of file to see if it's a binary file.  */

	  bool was_binary = set_binary_mode (current->desc, true);
	  off_t buffered;
	  file_block_read (current, current->bufsize);
	  buffered = current->buffered;

	  if (! was_binary)
	    {
	      /* Revert to text mode and seek back to the beginning to
		 reread the file.  Use relative seek, since file
		 descriptors like stdin might not start at offset
		 zero.  */

	      if (lseek (current->desc, - buffered, SEEK_CUR) == -1)
		pfatal_with_name (current->name);
	      set_binary_mode (current->desc, false);
	      current->buffered = 0;
	      current->eof = false;
	    }

	  return binary_file_p (current->buffer, buffered);
	}
    }

  current->buffered = 0;
  current->eof = false;
  return false;
}

/* Slurp the rest of the current file completely into memory.  */

static void
slurp (struct file_data *current)
{
  size_t cc;

  if (current->desc < 0)
    {
      /* The file is nonexistent.  */
      return;
    }

  if (S_ISREG (current->stat.st_mode))
    {
      /* It's a regular file; slurp in the rest all at once.  */

      /* Get the size out of the stat block.
	 Allocate just enough room for appended newline plus word sentinel,
	 plus word-alignment since we want the buffer word-aligned.  */
      size_t file_size = current->stat.st_size;
      cc = file_size + 2 * sizeof (word) - file_size % sizeof (word);
      if (file_size != current->stat.st_size || cc < file_size
	  || PTRDIFF_MAX <= cc)
	xalloc_die ();

      if (current->bufsize < cc)
	{
	  current->bufsize = cc;
	  current->buffer = xrealloc (current->buffer, cc);
	}

      /* Try to read at least 1 more byte than the size indicates, to
	 detect whether the file is growing.  This is a nicety for
	 users who run 'diff' on files while they are changing.  */

      if (current->buffered <= file_size)
	{
	  file_block_read (current, file_size + 1 - current->buffered);
	  if (current->buffered <= file_size)
	    return;
	}
    }

  /* It's not a regular file, or it's a growing regular file; read it,
     growing the buffer as needed.  */

  file_block_read (current, current->bufsize - current->buffered);

  if (current->buffered)
    {
      while (current->buffered == current->bufsize)
	{
	  if (PTRDIFF_MAX / 2 - sizeof (word) < current->bufsize)
	    xalloc_die ();
	  current->bufsize *= 2;
	  current->buffer = xrealloc (current->buffer, current->bufsize);
	  file_block_read (current, current->bufsize - current->buffered);
	}

      /* Allocate just enough room for appended newline plus word
	 sentinel, plus word-alignment.  */
      cc = current->buffered + 2 * sizeof (word);
      current->bufsize = cc - cc % sizeof (word);
      current->buffer = xrealloc (current->buffer, current->bufsize);
    }
}

/* Split the file into lines, simultaneously computing the equivalence
   class for each line.  */

static void
find_and_hash_each_line (struct file_data *current)
{
  hash_value h;
  char const *p = current->prefix_end;
  unsigned char c;
  lin i, *bucket;
  size_t length;

  /* Cache often-used quantities in local variables to help the compiler.  */
  char const **linbuf = current->linbuf;
  lin alloc_lines = current->alloc_lines;
  lin line = 0;
  lin linbuf_base = current->linbuf_base;
  lin *cureqs = xmalloc (alloc_lines * sizeof *cureqs);
  struct equivclass *eqs = equivs;
  lin eqs_index = equivs_index;
  lin eqs_alloc = equivs_alloc;
  char const *suffix_begin = current->suffix_begin;
  char const *bufend = FILE_BUFFER (current) + current->buffered;
  bool diff_length_compare_anyway =
    ignore_white_space != IGNORE_NO_WHITE_SPACE;
  bool same_length_diff_contents_compare_anyway =
    diff_length_compare_anyway | ignore_case;

  while (p < suffix_begin)
    {
      char const *ip = p;

      h = 0;

      /* Hash this line until we find a newline.  */
      if (ignore_case)
	switch (ignore_white_space)
	  {
	  case IGNORE_ALL_SPACE:
	    while ((c = *p++) != '\n')
	      if (! isspace (c))
		h = HASH (h, tolower (c));
	    break;

	  case IGNORE_SPACE_CHANGE:
	    while ((c = *p++) != '\n')
	      {
		if (isspace (c))
		  {
		    do
		      if ((c = *p++) == '\n')
			goto hashing_done;
		    while (isspace (c));

		    h = HASH (h, ' ');
		  }

		/* C is now the first non-space.  */
		h = HASH (h, tolower (c));
	      }
	    break;

	  case IGNORE_TAB_EXPANSION:
	    {
	      size_t column = 0;
	      while ((c = *p++) != '\n')
		{
		  size_t repetitions = 1;

		  switch (c)
		    {
		    case '\b':
		      column -= 0 < column;
		      break;

		    case '\t':
		      c = ' ';
		      repetitions = tabsize - column % tabsize;
		      column = (column + repetitions < column
				? 0
				: column + repetitions);
		      break;

		    case '\r':
		      column = 0;
		      break;

		    default:
		      c = tolower (c);
		      column++;
		      break;
		    }

		  do
		    h = HASH (h, c);
		  while (--repetitions != 0);
		}
	    }
	    break;

	  default:
	    while ((c = *p++) != '\n')
	      h = HASH (h, tolower (c));
	    break;
	  }
      else
	switch (ignore_white_space)
	  {
	  case IGNORE_ALL_SPACE:
	    while ((c = *p++) != '\n')
	      if (! isspace (c))
		h = HASH (h, c);
	    break;

	  case IGNORE_SPACE_CHANGE:
	    while ((c = *p++) != '\n')
	      {
		if (isspace (c))
		  {
		    do
		      if ((c = *p++) == '\n')
			goto hashing_done;
		    while (isspace (c));

		    h = HASH (h, ' ');
		  }

		/* C is now the first non-space.  */
		h = HASH (h, c);
	      }
	    break;

	  case IGNORE_TAB_EXPANSION:
	    {
	      size_t column = 0;
	      while ((c = *p++) != '\n')
		{
		  size_t repetitions = 1;

		  switch (c)
		    {
		    case '\b':
		      column -= 0 < column;
		      break;

		    case '\t':
		      c = ' ';
		      repetitions = tabsize - column % tabsize;
		      column = (column + repetitions < column
				? 0
				: column + repetitions);
		      break;

		    case '\r':
		      column = 0;
		      break;

		    default:
		      column++;
		      break;
		    }

		  do
		    h = HASH (h, c);
		  while (--repetitions != 0);
		}
	    }
	    break;

	  default:
	    while ((c = *p++) != '\n')
	      h = HASH (h, c);
	    break;
	  }

   hashing_done:;

      bucket = &buckets[h % nbuckets];
      length = p - ip - 1;

      if (p == bufend
	  && current->missing_newline
	  && ROBUST_OUTPUT_STYLE (output_style))
	{
	  /* This line is incomplete.  If this is significant,
	     put the line into buckets[-1].  */
	  if (ignore_white_space < IGNORE_SPACE_CHANGE)
	    bucket = &buckets[-1];

	  /* Omit the inserted newline when computing linbuf later.  */
	  p--;
	  bufend = suffix_begin = p;
	}

      for (i = *bucket;  ;  i = eqs[i].next)
	if (!i)
	  {
	    /* Create a new equivalence class in this bucket.  */
	    i = eqs_index++;
	    if (i == eqs_alloc)
	      {
		if (PTRDIFF_MAX / (2 * sizeof *eqs) <= eqs_alloc)
		  xalloc_die ();
		eqs_alloc *= 2;
		eqs = xrealloc (eqs, eqs_alloc * sizeof *eqs);
	      }
	    eqs[i].next = *bucket;
	    eqs[i].hash = h;
	    eqs[i].line = ip;
	    eqs[i].length = length;
	    *bucket = i;
	    break;
	  }
	else if (eqs[i].hash == h)
	  {
	    char const *eqline = eqs[i].line;

	    /* Reuse existing class if lines_differ reports the lines
               equal.  */
	    if (eqs[i].length == length)
	      {
		/* Reuse existing equivalence class if the lines are identical.
		   This detects the common case of exact identity
		   faster than lines_differ would.  */
		if (memcmp (eqline, ip, length) == 0)
		  break;
		if (!same_length_diff_contents_compare_anyway)
		  continue;
	      }
	    else if (!diff_length_compare_anyway)
	      continue;

	    if (! lines_differ (eqline, ip))
	      break;
	  }

      /* Maybe increase the size of the line table.  */
      if (line == alloc_lines)
	{
	  /* Double (alloc_lines - linbuf_base) by adding to alloc_lines.  */
	  if (PTRDIFF_MAX / 3 <= alloc_lines
	      || PTRDIFF_MAX / sizeof *cureqs <= 2 * alloc_lines - linbuf_base
	      || PTRDIFF_MAX / sizeof *linbuf <= alloc_lines - linbuf_base)
	    xalloc_die ();
	  alloc_lines = 2 * alloc_lines - linbuf_base;
	  cureqs = xrealloc (cureqs, alloc_lines * sizeof *cureqs);
	  linbuf += linbuf_base;
	  linbuf = xrealloc (linbuf,
			     (alloc_lines - linbuf_base) * sizeof *linbuf);
	  linbuf -= linbuf_base;
	}
      linbuf[line] = ip;
      cureqs[line] = i;
      ++line;
    }

  current->buffered_lines = line;

  for (i = 0;  ;  i++)
    {
      /* Record the line start for lines in the suffix that we care about.
	 Record one more line start than lines,
	 so that we can compute the length of any buffered line.  */
      if (line == alloc_lines)
	{
	  /* Double (alloc_lines - linbuf_base) by adding to alloc_lines.  */
	  if (PTRDIFF_MAX / 3 <= alloc_lines
	      || PTRDIFF_MAX / sizeof *cureqs <= 2 * alloc_lines - linbuf_base
	      || PTRDIFF_MAX / sizeof *linbuf <= alloc_lines - linbuf_base)
	    xalloc_die ();
	  alloc_lines = 2 * alloc_lines - linbuf_base;
	  linbuf += linbuf_base;
	  linbuf = xrealloc (linbuf,
			     (alloc_lines - linbuf_base) * sizeof *linbuf);
	  linbuf -= linbuf_base;
	}
      linbuf[line] = p;

      if (p == bufend)
	break;

      if (context <= i && no_diff_means_no_output)
	break;

      line++;

      while (*p++ != '\n')
	continue;
    }

  /* Done with cache in local variables.  */
  current->linbuf = linbuf;
  current->valid_lines = line;
  current->alloc_lines = alloc_lines;
  current->equivs = cureqs;
  equivs = eqs;
  equivs_alloc = eqs_alloc;
  equivs_index = eqs_index;
}

/* Prepare the text.  Make sure the text end is initialized.
   Make sure text ends in a newline,
   but remember that we had to add one.
   Strip trailing CRs, if that was requested.  */

static void
prepare_text (struct file_data *current)
{
  size_t buffered = current->buffered;
  char *p = FILE_BUFFER (current);
  char *dst;

  if (buffered == 0 || p[buffered - 1] == '\n')
    current->missing_newline = false;
  else
    {
      p[buffered++] = '\n';
      current->missing_newline = true;
    }

  if (!p)
    return;

  /* Don't use uninitialized storage when planting or using sentinels.  */
  memset (p + buffered, 0, sizeof (word));

  if (strip_trailing_cr && (dst = memchr (p, '\r', buffered)))
    {
      char const *src = dst;
      char const *srclim = p + buffered;

      do
	dst += ! ((*dst = *src++) == '\r' && *src == '\n');
      while (src < srclim);

      buffered -= src - dst;
    }

  current->buffered = buffered;
}

/* We have found N lines in a buffer of size S; guess the
   proportionate number of lines that will be found in a buffer of
   size T.  However, do not guess a number of lines so large that the
   resulting line table might cause overflow in size calculations.  */
static lin
guess_lines (lin n, size_t s, size_t t)
{
  size_t guessed_bytes_per_line = n < 10 ? 32 : s / (n - 1);
  lin guessed_lines = MAX (1, t / guessed_bytes_per_line);
  return MIN (guessed_lines, PTRDIFF_MAX / (2 * sizeof (char *) + 1) - 5) + 5;
}

/* Given a vector of two file_data objects, find the identical
   prefixes and suffixes of each object.  */

static void
find_identical_ends (struct file_data filevec[])
{
  word *w0, *w1;
  char *p0, *p1, *buffer0, *buffer1;
  char const *end0, *beg0;
  char const **linbuf0, **linbuf1;
  lin i, lines;
  size_t n0, n1;
  lin alloc_lines0, alloc_lines1;
  lin buffered_prefix, prefix_count, prefix_mask;
  lin middle_guess, suffix_guess;

  slurp (&filevec[0]);
  prepare_text (&filevec[0]);
  if (filevec[0].desc != filevec[1].desc)
    {
      slurp (&filevec[1]);
      prepare_text (&filevec[1]);
    }
  else
    {
      filevec[1].buffer = filevec[0].buffer;
      filevec[1].bufsize = filevec[0].bufsize;
      filevec[1].buffered = filevec[0].buffered;
      filevec[1].missing_newline = filevec[0].missing_newline;
    }

  /* Find identical prefix.  */

  w0 = filevec[0].buffer;
  w1 = filevec[1].buffer;
  p0 = buffer0 = (char *) w0;
  p1 = buffer1 = (char *) w1;
  n0 = filevec[0].buffered;
  n1 = filevec[1].buffered;

  if (p0 == p1)
    /* The buffers are the same; sentinels won't work.  */
    p0 = p1 += n1;
  else
    {
      /* Insert end sentinels, in this case characters that are guaranteed
	 to make the equality test false, and thus terminate the loop.  */

      if (n0 < n1)
	p0[n0] = ~p1[n0];
      else
	p1[n1] = ~p0[n1];

      /* Loop until first mismatch, or to the sentinel characters.  */

      /* Compare a word at a time for speed.  */
      while (*w0 == *w1)
	w0++, w1++;

      /* Do the last few bytes of comparison a byte at a time.  */
      p0 = (char *) w0;
      p1 = (char *) w1;
      while (*p0 == *p1)
	p0++, p1++;

      /* Don't mistakenly count missing newline as part of prefix.  */
      if (ROBUST_OUTPUT_STYLE (output_style)
	  && ((buffer0 + n0 - filevec[0].missing_newline < p0)
	      !=
	      (buffer1 + n1 - filevec[1].missing_newline < p1)))
	p0--, p1--;
    }

  /* Now P0 and P1 point at the first nonmatching characters.  */

  /* Skip back to last line-beginning in the prefix,
     and then discard up to HORIZON_LINES lines from the prefix.  */
  i = horizon_lines;
  while (p0 != buffer0 && (p0[-1] != '\n' || i--))
    p0--, p1--;

  /* Record the prefix.  */
  filevec[0].prefix_end = p0;
  filevec[1].prefix_end = p1;

  /* Find identical suffix.  */

  /* P0 and P1 point beyond the last chars not yet compared.  */
  p0 = buffer0 + n0;
  p1 = buffer1 + n1;

  if (! ROBUST_OUTPUT_STYLE (output_style)
      || filevec[0].missing_newline == filevec[1].missing_newline)
    {
      end0 = p0;	/* Addr of last char in file 0.  */

      /* Get value of P0 at which we should stop scanning backward:
	 this is when either P0 or P1 points just past the last char
	 of the identical prefix.  */
      beg0 = filevec[0].prefix_end + (n0 < n1 ? 0 : n0 - n1);

      /* Scan back until chars don't match or we reach that point.  */
      for (; p0 != beg0; p0--, p1--)
	if (*p0 != *p1)
	  {
	    /* Point at the first char of the matching suffix.  */
	    beg0 = p0;
	    break;
	  }

      /* Are we at a line-beginning in both files?  If not, add the rest of
	 this line to the main body.  Discard up to HORIZON_LINES lines from
	 the identical suffix.  Also, discard one extra line,
	 because shift_boundaries may need it.  */
      i = horizon_lines + !((buffer0 == p0 || p0[-1] == '\n')
			    &&
			    (buffer1 == p1 || p1[-1] == '\n'));
      while (i-- && p0 != end0)
	while (*p0++ != '\n')
	  continue;

      p1 += p0 - beg0;
    }

  /* Record the suffix.  */
  filevec[0].suffix_begin = p0;
  filevec[1].suffix_begin = p1;

  /* Calculate number of lines of prefix to save.

     prefix_count == 0 means save the whole prefix;
     we need this for options like -D that output the whole file,
     or for enormous contexts (to avoid worrying about arithmetic overflow).
     We also need it for options like -F that output some preceding line;
     at least we will need to find the last few lines,
     but since we don't know how many, it's easiest to find them all.

     Otherwise, prefix_count != 0.  Save just prefix_count lines at start
     of the line buffer; they'll be moved to the proper location later.
     Handle 1 more line than the context says (because we count 1 too many),
     rounded up to the next power of 2 to speed index computation.  */

  if (no_diff_means_no_output && ! function_regexp.fastmap
      && context < LIN_MAX / 4 && context < n0)
    {
      middle_guess = guess_lines (0, 0, p0 - filevec[0].prefix_end);
      suffix_guess = guess_lines (0, 0, buffer0 + n0 - p0);
      for (prefix_count = 1;  prefix_count <= context;  prefix_count *= 2)
	continue;
      alloc_lines0 = (prefix_count + middle_guess
		      + MIN (context, suffix_guess));
    }
  else
    {
      prefix_count = 0;
      alloc_lines0 = guess_lines (0, 0, n0);
    }

  prefix_mask = prefix_count - 1;
  lines = 0;
  linbuf0 = xmalloc (alloc_lines0 * sizeof *linbuf0);
  p0 = buffer0;

  /* If the prefix is needed, find the prefix lines.  */
  if (! (no_diff_means_no_output
	 && filevec[0].prefix_end == p0
	 && filevec[1].prefix_end == p1))
    {
      end0 = filevec[0].prefix_end;
      while (p0 != end0)
	{
	  lin l = lines++ & prefix_mask;
	  if (l == alloc_lines0)
	    {
	      if (PTRDIFF_MAX / (2 * sizeof *linbuf0) <= alloc_lines0)
		xalloc_die ();
	      alloc_lines0 *= 2;
	      linbuf0 = xrealloc (linbuf0, alloc_lines0 * sizeof *linbuf0);
	    }
	  linbuf0[l] = p0;
	  while (*p0++ != '\n')
	    continue;
	}
    }
  buffered_prefix = prefix_count && context < lines ? context : lines;

  /* Allocate line buffer 1.  */

  middle_guess = guess_lines (lines, p0 - buffer0, p1 - filevec[1].prefix_end);
  suffix_guess = guess_lines (lines, p0 - buffer0, buffer1 + n1 - p1);
  alloc_lines1 = buffered_prefix + middle_guess + MIN (context, suffix_guess);
  if (alloc_lines1 < buffered_prefix
      || PTRDIFF_MAX / sizeof *linbuf1 <= alloc_lines1)
    xalloc_die ();
  linbuf1 = xmalloc (alloc_lines1 * sizeof *linbuf1);

  if (buffered_prefix != lines)
    {
      /* Rotate prefix lines to proper location.  */
      for (i = 0;  i < buffered_prefix;  i++)
	linbuf1[i] = linbuf0[(lines - context + i) & prefix_mask];
      for (i = 0;  i < buffered_prefix;  i++)
	linbuf0[i] = linbuf1[i];
    }

  /* Initialize line buffer 1 from line buffer 0.  */
  for (i = 0; i < buffered_prefix; i++)
    linbuf1[i] = linbuf0[i] - buffer0 + buffer1;

  /* Record the line buffer, adjusted so that
     linbuf[0] points at the first differing line.  */
  filevec[0].linbuf = linbuf0 + buffered_prefix;
  filevec[1].linbuf = linbuf1 + buffered_prefix;
  filevec[0].linbuf_base = filevec[1].linbuf_base = - buffered_prefix;
  filevec[0].alloc_lines = alloc_lines0 - buffered_prefix;
  filevec[1].alloc_lines = alloc_lines1 - buffered_prefix;
  filevec[0].prefix_lines = filevec[1].prefix_lines = lines;
}

/* If 1 < k, then (2**k - prime_offset[k]) is the largest prime less
   than 2**k.  This table is derived from Chris K. Caldwell's list
   <http://www.utm.edu/research/primes/lists/2small/>.  */

static unsigned char const prime_offset[] =
{
  0, 0, 1, 1, 3, 1, 3, 1, 5, 3, 3, 9, 3, 1, 3, 19, 15, 1, 5, 1, 3, 9, 3,
  15, 3, 39, 5, 39, 57, 3, 35, 1, 5, 9, 41, 31, 5, 25, 45, 7, 87, 21,
  11, 57, 17, 55, 21, 115, 59, 81, 27, 129, 47, 111, 33, 55, 5, 13, 27,
  55, 93, 1, 57, 25
};

/* Verify that this host's size_t is not too wide for the above table.  */

verify (enough_prime_offsets,
	sizeof (size_t) * CHAR_BIT <= sizeof prime_offset);

/* Given a vector of two file_data objects, read the file associated
   with each one, and build the table of equivalence classes.
   Return nonzero if either file appears to be a binary file.
   If PRETEND_BINARY is nonzero, pretend they are binary regardless.  */

bool
read_files (struct file_data filevec[], bool pretend_binary)
{
  int i;
  bool skip_test = text | pretend_binary;
  bool appears_binary = pretend_binary | sip (&filevec[0], skip_test);

  if (filevec[0].desc != filevec[1].desc)
    appears_binary |= sip (&filevec[1], skip_test | appears_binary);
  else
    {
      filevec[1].buffer = filevec[0].buffer;
      filevec[1].bufsize = filevec[0].bufsize;
      filevec[1].buffered = filevec[0].buffered;
    }
  if (appears_binary)
    {
      set_binary_mode (filevec[0].desc, true);
      set_binary_mode (filevec[1].desc, true);
      return true;
    }

  find_identical_ends (filevec);

  equivs_alloc = filevec[0].alloc_lines + filevec[1].alloc_lines + 1;
  if (PTRDIFF_MAX / sizeof *equivs <= equivs_alloc)
    xalloc_die ();
  equivs = xmalloc (equivs_alloc * sizeof *equivs);
  /* Equivalence class 0 is permanently safe for lines that were not
     hashed.  Real equivalence classes start at 1.  */
  equivs_index = 1;

  /* Allocate (one plus) a prime number of hash buckets.  Use a prime
     number between 1/3 and 2/3 of the value of equiv_allocs,
     approximately.  */
  for (i = 9; (size_t) 1 << i < equivs_alloc / 3; i++)
    continue;
  nbuckets = ((size_t) 1 << i) - prime_offset[i];
  if (PTRDIFF_MAX / sizeof *buckets <= nbuckets)
    xalloc_die ();
  buckets = zalloc ((nbuckets + 1) * sizeof *buckets);
  buckets++;

  for (i = 0; i < 2; i++)
    find_and_hash_each_line (&filevec[i]);

  filevec[0].equiv_max = filevec[1].equiv_max = equivs_index;

  free (equivs);
  free (buckets - 1);

  return false;
}
