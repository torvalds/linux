/* Extended regular expression matching and search library.
   Copyright (C) 2002-2006, 2010, 2011 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Isamu Hasegawa <isamu@yamato.ibm.com>.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

static void re_string_construct_common (const char *str, int len,
					re_string_t *pstr,
					RE_TRANSLATE_TYPE trans, int icase,
					const re_dfa_t *dfa) internal_function;
static re_dfastate_t *create_ci_newstate (const re_dfa_t *dfa,
					  const re_node_set *nodes,
					  unsigned int hash) internal_function;
static re_dfastate_t *create_cd_newstate (const re_dfa_t *dfa,
					  const re_node_set *nodes,
					  unsigned int context,
					  unsigned int hash) internal_function;

/* Functions for string operation.  */

/* This function allocate the buffers.  It is necessary to call
   re_string_reconstruct before using the object.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_string_allocate (re_string_t *pstr, const char *str, int len, int init_len,
		    RE_TRANSLATE_TYPE trans, int icase, const re_dfa_t *dfa)
{
  reg_errcode_t ret;
  int init_buf_len;

  /* Ensure at least one character fits into the buffers.  */
  if (init_len < dfa->mb_cur_max)
    init_len = dfa->mb_cur_max;
  init_buf_len = (len + 1 < init_len) ? len + 1: init_len;
  re_string_construct_common (str, len, pstr, trans, icase, dfa);

  ret = re_string_realloc_buffers (pstr, init_buf_len);
  if (BE (ret != REG_NOERROR, 0))
    return ret;

  pstr->word_char = dfa->word_char;
  pstr->word_ops_used = dfa->word_ops_used;
  pstr->mbs = pstr->mbs_allocated ? pstr->mbs : (unsigned char *) str;
  pstr->valid_len = (pstr->mbs_allocated || dfa->mb_cur_max > 1) ? 0 : len;
  pstr->valid_raw_len = pstr->valid_len;
  return REG_NOERROR;
}

/* This function allocate the buffers, and initialize them.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_string_construct (re_string_t *pstr, const char *str, int len,
		     RE_TRANSLATE_TYPE trans, int icase, const re_dfa_t *dfa)
{
  reg_errcode_t ret;
  memset (pstr, '\0', sizeof (re_string_t));
  re_string_construct_common (str, len, pstr, trans, icase, dfa);

  if (len > 0)
    {
      ret = re_string_realloc_buffers (pstr, len + 1);
      if (BE (ret != REG_NOERROR, 0))
	return ret;
    }
  pstr->mbs = pstr->mbs_allocated ? pstr->mbs : (unsigned char *) str;

  if (icase)
    {
#ifdef RE_ENABLE_I18N
      if (dfa->mb_cur_max > 1)
	{
	  while (1)
	    {
	      ret = build_wcs_upper_buffer (pstr);
	      if (BE (ret != REG_NOERROR, 0))
		return ret;
	      if (pstr->valid_raw_len >= len)
		break;
	      if (pstr->bufs_len > pstr->valid_len + dfa->mb_cur_max)
		break;
	      ret = re_string_realloc_buffers (pstr, pstr->bufs_len * 2);
	      if (BE (ret != REG_NOERROR, 0))
		return ret;
	    }
	}
      else
#endif /* RE_ENABLE_I18N  */
	build_upper_buffer (pstr);
    }
  else
    {
#ifdef RE_ENABLE_I18N
      if (dfa->mb_cur_max > 1)
	build_wcs_buffer (pstr);
      else
#endif /* RE_ENABLE_I18N  */
	{
	  if (trans != NULL)
	    re_string_translate_buffer (pstr);
	  else
	    {
	      pstr->valid_len = pstr->bufs_len;
	      pstr->valid_raw_len = pstr->bufs_len;
	    }
	}
    }

  return REG_NOERROR;
}

/* Helper functions for re_string_allocate, and re_string_construct.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_string_realloc_buffers (re_string_t *pstr, int new_buf_len)
{
#ifdef RE_ENABLE_I18N
  if (pstr->mb_cur_max > 1)
    {
      wint_t *new_wcs;

      /* Avoid overflow in realloc.  */
      const size_t max_object_size = MAX (sizeof (wint_t), sizeof (int));
      if (BE (SIZE_MAX / max_object_size < new_buf_len, 0))
	return REG_ESPACE;

      new_wcs = re_realloc (pstr->wcs, wint_t, new_buf_len);
      if (BE (new_wcs == NULL, 0))
	return REG_ESPACE;
      pstr->wcs = new_wcs;
      if (pstr->offsets != NULL)
	{
	  int *new_offsets = re_realloc (pstr->offsets, int, new_buf_len);
	  if (BE (new_offsets == NULL, 0))
	    return REG_ESPACE;
	  pstr->offsets = new_offsets;
	}
    }
#endif /* RE_ENABLE_I18N  */
  if (pstr->mbs_allocated)
    {
      unsigned char *new_mbs = re_realloc (pstr->mbs, unsigned char,
					   new_buf_len);
      if (BE (new_mbs == NULL, 0))
	return REG_ESPACE;
      pstr->mbs = new_mbs;
    }
  pstr->bufs_len = new_buf_len;
  return REG_NOERROR;
}


static void
internal_function
re_string_construct_common (const char *str, int len, re_string_t *pstr,
			    RE_TRANSLATE_TYPE trans, int icase,
			    const re_dfa_t *dfa)
{
  pstr->raw_mbs = (const unsigned char *) str;
  pstr->len = len;
  pstr->raw_len = len;
  pstr->trans = trans;
  pstr->icase = icase ? 1 : 0;
  pstr->mbs_allocated = (trans != NULL || icase);
  pstr->mb_cur_max = dfa->mb_cur_max;
  pstr->is_utf8 = dfa->is_utf8;
  pstr->map_notascii = dfa->map_notascii;
  pstr->stop = pstr->len;
  pstr->raw_stop = pstr->stop;
}

#ifdef RE_ENABLE_I18N

/* Build wide character buffer PSTR->WCS.
   If the byte sequence of the string are:
     <mb1>(0), <mb1>(1), <mb2>(0), <mb2>(1), <sb3>
   Then wide character buffer will be:
     <wc1>   , WEOF    , <wc2>   , WEOF    , <wc3>
   We use WEOF for padding, they indicate that the position isn't
   a first byte of a multibyte character.

   Note that this function assumes PSTR->VALID_LEN elements are already
   built and starts from PSTR->VALID_LEN.  */

static void
internal_function
build_wcs_buffer (re_string_t *pstr)
{
#ifdef _LIBC
  unsigned char buf[MB_LEN_MAX];
  assert (MB_LEN_MAX >= pstr->mb_cur_max);
#else
  unsigned char buf[64];
#endif
  mbstate_t prev_st;
  int byte_idx, end_idx, remain_len;
  size_t mbclen;

  /* Build the buffers from pstr->valid_len to either pstr->len or
     pstr->bufs_len.  */
  end_idx = (pstr->bufs_len > pstr->len) ? pstr->len : pstr->bufs_len;
  for (byte_idx = pstr->valid_len; byte_idx < end_idx;)
    {
      wchar_t wc;
      const char *p;

      remain_len = end_idx - byte_idx;
      prev_st = pstr->cur_state;
      /* Apply the translation if we need.  */
      if (BE (pstr->trans != NULL, 0))
	{
	  int i, ch;

	  for (i = 0; i < pstr->mb_cur_max && i < remain_len; ++i)
	    {
	      ch = pstr->raw_mbs [pstr->raw_mbs_idx + byte_idx + i];
	      buf[i] = pstr->mbs[byte_idx + i] = pstr->trans[ch];
	    }
	  p = (const char *) buf;
	}
      else
	p = (const char *) pstr->raw_mbs + pstr->raw_mbs_idx + byte_idx;
      mbclen = __mbrtowc (&wc, p, remain_len, &pstr->cur_state);
      if (BE (mbclen == (size_t) -1 || mbclen == 0
	      || (mbclen == (size_t) -2 && pstr->bufs_len >= pstr->len), 0))
	{
	  /* We treat these cases as a singlebyte character.  */
	  mbclen = 1;
	  wc = (wchar_t) pstr->raw_mbs[pstr->raw_mbs_idx + byte_idx];
	  if (BE (pstr->trans != NULL, 0))
	    wc = pstr->trans[wc];
	  pstr->cur_state = prev_st;
	}
      else if (BE (mbclen == (size_t) -2, 0))
	{
	  /* The buffer doesn't have enough space, finish to build.  */
	  pstr->cur_state = prev_st;
	  break;
	}

      /* Write wide character and padding.  */
      pstr->wcs[byte_idx++] = wc;
      /* Write paddings.  */
      for (remain_len = byte_idx + mbclen - 1; byte_idx < remain_len ;)
	pstr->wcs[byte_idx++] = WEOF;
    }
  pstr->valid_len = byte_idx;
  pstr->valid_raw_len = byte_idx;
}

/* Build wide character buffer PSTR->WCS like build_wcs_buffer,
   but for REG_ICASE.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
build_wcs_upper_buffer (re_string_t *pstr)
{
  mbstate_t prev_st;
  int src_idx, byte_idx, end_idx, remain_len;
  size_t mbclen;
#ifdef _LIBC
  char buf[MB_LEN_MAX];
  assert (MB_LEN_MAX >= pstr->mb_cur_max);
#else
  char buf[64];
#endif

  byte_idx = pstr->valid_len;
  end_idx = (pstr->bufs_len > pstr->len) ? pstr->len : pstr->bufs_len;

  /* The following optimization assumes that ASCII characters can be
     mapped to wide characters with a simple cast.  */
  if (! pstr->map_notascii && pstr->trans == NULL && !pstr->offsets_needed)
    {
      while (byte_idx < end_idx)
	{
	  wchar_t wc;

	  if (isascii (pstr->raw_mbs[pstr->raw_mbs_idx + byte_idx])
	      && mbsinit (&pstr->cur_state))
	    {
	      /* In case of a singlebyte character.  */
	      pstr->mbs[byte_idx]
		= toupper (pstr->raw_mbs[pstr->raw_mbs_idx + byte_idx]);
	      /* The next step uses the assumption that wchar_t is encoded
		 ASCII-safe: all ASCII values can be converted like this.  */
	      pstr->wcs[byte_idx] = (wchar_t) pstr->mbs[byte_idx];
	      ++byte_idx;
	      continue;
	    }

	  remain_len = end_idx - byte_idx;
	  prev_st = pstr->cur_state;
	  mbclen = __mbrtowc (&wc,
			      ((const char *) pstr->raw_mbs + pstr->raw_mbs_idx
			       + byte_idx), remain_len, &pstr->cur_state);
	  if (BE (mbclen + 2 > 2, 1))
	    {
	      wchar_t wcu = wc;
	      if (iswlower (wc))
		{
		  size_t mbcdlen;

		  wcu = towupper (wc);
		  mbcdlen = wcrtomb (buf, wcu, &prev_st);
		  if (BE (mbclen == mbcdlen, 1))
		    memcpy (pstr->mbs + byte_idx, buf, mbclen);
		  else
		    {
		      src_idx = byte_idx;
		      goto offsets_needed;
		    }
		}
	      else
		memcpy (pstr->mbs + byte_idx,
			pstr->raw_mbs + pstr->raw_mbs_idx + byte_idx, mbclen);
	      pstr->wcs[byte_idx++] = wcu;
	      /* Write paddings.  */
	      for (remain_len = byte_idx + mbclen - 1; byte_idx < remain_len ;)
		pstr->wcs[byte_idx++] = WEOF;
	    }
	  else if (mbclen == (size_t) -1 || mbclen == 0
		   || (mbclen == (size_t) -2 && pstr->bufs_len >= pstr->len))
	    {
	      /* It is an invalid character, an incomplete character
		 at the end of the string, or '\0'.  Just use the byte.  */
	      int ch = pstr->raw_mbs[pstr->raw_mbs_idx + byte_idx];
	      pstr->mbs[byte_idx] = ch;
	      /* And also cast it to wide char.  */
	      pstr->wcs[byte_idx++] = (wchar_t) ch;
	      if (BE (mbclen == (size_t) -1, 0))
		pstr->cur_state = prev_st;
	    }
	  else
	    {
	      /* The buffer doesn't have enough space, finish to build.  */
	      pstr->cur_state = prev_st;
	      break;
	    }
	}
      pstr->valid_len = byte_idx;
      pstr->valid_raw_len = byte_idx;
      return REG_NOERROR;
    }
  else
    for (src_idx = pstr->valid_raw_len; byte_idx < end_idx;)
      {
	wchar_t wc;
	const char *p;
      offsets_needed:
	remain_len = end_idx - byte_idx;
	prev_st = pstr->cur_state;
	if (BE (pstr->trans != NULL, 0))
	  {
	    int i, ch;

	    for (i = 0; i < pstr->mb_cur_max && i < remain_len; ++i)
	      {
		ch = pstr->raw_mbs [pstr->raw_mbs_idx + src_idx + i];
		buf[i] = pstr->trans[ch];
	      }
	    p = (const char *) buf;
	  }
	else
	  p = (const char *) pstr->raw_mbs + pstr->raw_mbs_idx + src_idx;
	mbclen = __mbrtowc (&wc, p, remain_len, &pstr->cur_state);
	if (BE (mbclen + 2 > 2, 1))
	  {
	    wchar_t wcu = wc;
	    if (iswlower (wc))
	      {
		size_t mbcdlen;

		wcu = towupper (wc);
		mbcdlen = wcrtomb ((char *) buf, wcu, &prev_st);
		if (BE (mbclen == mbcdlen, 1))
		  memcpy (pstr->mbs + byte_idx, buf, mbclen);
		else if (mbcdlen != (size_t) -1)
		  {
		    size_t i;

		    if (byte_idx + mbcdlen > pstr->bufs_len)
		      {
			pstr->cur_state = prev_st;
			break;
		      }

		    if (pstr->offsets == NULL)
		      {
			pstr->offsets = re_malloc (int, pstr->bufs_len);

			if (pstr->offsets == NULL)
			  return REG_ESPACE;
		      }
		    if (!pstr->offsets_needed)
		      {
			for (i = 0; i < (size_t) byte_idx; ++i)
			  pstr->offsets[i] = i;
			pstr->offsets_needed = 1;
		      }

		    memcpy (pstr->mbs + byte_idx, buf, mbcdlen);
		    pstr->wcs[byte_idx] = wcu;
		    pstr->offsets[byte_idx] = src_idx;
		    for (i = 1; i < mbcdlen; ++i)
		      {
			pstr->offsets[byte_idx + i]
			  = src_idx + (i < mbclen ? i : mbclen - 1);
			pstr->wcs[byte_idx + i] = WEOF;
		      }
		    pstr->len += mbcdlen - mbclen;
		    if (pstr->raw_stop > src_idx)
		      pstr->stop += mbcdlen - mbclen;
		    end_idx = (pstr->bufs_len > pstr->len)
			      ? pstr->len : pstr->bufs_len;
		    byte_idx += mbcdlen;
		    src_idx += mbclen;
		    continue;
		  }
		else
		  memcpy (pstr->mbs + byte_idx, p, mbclen);
	      }
	    else
	      memcpy (pstr->mbs + byte_idx, p, mbclen);

	    if (BE (pstr->offsets_needed != 0, 0))
	      {
		size_t i;
		for (i = 0; i < mbclen; ++i)
		  pstr->offsets[byte_idx + i] = src_idx + i;
	      }
	    src_idx += mbclen;

	    pstr->wcs[byte_idx++] = wcu;
	    /* Write paddings.  */
	    for (remain_len = byte_idx + mbclen - 1; byte_idx < remain_len ;)
	      pstr->wcs[byte_idx++] = WEOF;
	  }
	else if (mbclen == (size_t) -1 || mbclen == 0
		 || (mbclen == (size_t) -2 && pstr->bufs_len >= pstr->len))
	  {
	    /* It is an invalid character or '\0'.  Just use the byte.  */
	    int ch = pstr->raw_mbs[pstr->raw_mbs_idx + src_idx];

	    if (BE (pstr->trans != NULL, 0))
	      ch = pstr->trans [ch];
	    pstr->mbs[byte_idx] = ch;

	    if (BE (pstr->offsets_needed != 0, 0))
	      pstr->offsets[byte_idx] = src_idx;
	    ++src_idx;

	    /* And also cast it to wide char.  */
	    pstr->wcs[byte_idx++] = (wchar_t) ch;
	    if (BE (mbclen == (size_t) -1, 0))
	      pstr->cur_state = prev_st;
	  }
	else
	  {
	    /* The buffer doesn't have enough space, finish to build.  */
	    pstr->cur_state = prev_st;
	    break;
	  }
      }
  pstr->valid_len = byte_idx;
  pstr->valid_raw_len = src_idx;
  return REG_NOERROR;
}

/* Skip characters until the index becomes greater than NEW_RAW_IDX.
   Return the index.  */

static int
internal_function
re_string_skip_chars (re_string_t *pstr, int new_raw_idx, wint_t *last_wc)
{
  mbstate_t prev_st;
  int rawbuf_idx;
  size_t mbclen;
  wint_t wc = WEOF;

  /* Skip the characters which are not necessary to check.  */
  for (rawbuf_idx = pstr->raw_mbs_idx + pstr->valid_raw_len;
       rawbuf_idx < new_raw_idx;)
    {
      wchar_t wc2;
      int remain_len = pstr->raw_len - rawbuf_idx;
      prev_st = pstr->cur_state;
      mbclen = __mbrtowc (&wc2, (const char *) pstr->raw_mbs + rawbuf_idx,
			  remain_len, &pstr->cur_state);
      if (BE ((ssize_t) mbclen <= 0, 0))
	{
	  /* We treat these cases as a single byte character.  */
	  if (mbclen == 0 || remain_len == 0)
	    wc = L'\0';
	  else
	    wc = *(unsigned char *) (pstr->raw_mbs + rawbuf_idx);
	  mbclen = 1;
	  pstr->cur_state = prev_st;
	}
      else
	wc = (wint_t) wc2;
      /* Then proceed the next character.  */
      rawbuf_idx += mbclen;
    }
  *last_wc = wc;
  return rawbuf_idx;
}
#endif /* RE_ENABLE_I18N  */

/* Build the buffer PSTR->MBS, and apply the translation if we need.
   This function is used in case of REG_ICASE.  */

static void
internal_function
build_upper_buffer (re_string_t *pstr)
{
  int char_idx, end_idx;
  end_idx = (pstr->bufs_len > pstr->len) ? pstr->len : pstr->bufs_len;

  for (char_idx = pstr->valid_len; char_idx < end_idx; ++char_idx)
    {
      int ch = pstr->raw_mbs[pstr->raw_mbs_idx + char_idx];
      if (BE (pstr->trans != NULL, 0))
	ch = pstr->trans[ch];
      if (islower (ch))
	pstr->mbs[char_idx] = toupper (ch);
      else
	pstr->mbs[char_idx] = ch;
    }
  pstr->valid_len = char_idx;
  pstr->valid_raw_len = char_idx;
}

/* Apply TRANS to the buffer in PSTR.  */

static void
internal_function
re_string_translate_buffer (re_string_t *pstr)
{
  int buf_idx, end_idx;
  end_idx = (pstr->bufs_len > pstr->len) ? pstr->len : pstr->bufs_len;

  for (buf_idx = pstr->valid_len; buf_idx < end_idx; ++buf_idx)
    {
      int ch = pstr->raw_mbs[pstr->raw_mbs_idx + buf_idx];
      pstr->mbs[buf_idx] = pstr->trans[ch];
    }

  pstr->valid_len = buf_idx;
  pstr->valid_raw_len = buf_idx;
}

/* This function re-construct the buffers.
   Concretely, convert to wide character in case of pstr->mb_cur_max > 1,
   convert to upper case in case of REG_ICASE, apply translation.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_string_reconstruct (re_string_t *pstr, int idx, int eflags)
{
  int offset = idx - pstr->raw_mbs_idx;
  if (BE (offset < 0, 0))
    {
      /* Reset buffer.  */
#ifdef RE_ENABLE_I18N
      if (pstr->mb_cur_max > 1)
	memset (&pstr->cur_state, '\0', sizeof (mbstate_t));
#endif /* RE_ENABLE_I18N */
      pstr->len = pstr->raw_len;
      pstr->stop = pstr->raw_stop;
      pstr->valid_len = 0;
      pstr->raw_mbs_idx = 0;
      pstr->valid_raw_len = 0;
      pstr->offsets_needed = 0;
      pstr->tip_context = ((eflags & REG_NOTBOL) ? CONTEXT_BEGBUF
			   : CONTEXT_NEWLINE | CONTEXT_BEGBUF);
      if (!pstr->mbs_allocated)
	pstr->mbs = (unsigned char *) pstr->raw_mbs;
      offset = idx;
    }

  if (BE (offset != 0, 1))
    {
      /* Should the already checked characters be kept?  */
      if (BE (offset < pstr->valid_raw_len, 1))
	{
	  /* Yes, move them to the front of the buffer.  */
#ifdef RE_ENABLE_I18N
	  if (BE (pstr->offsets_needed, 0))
	    {
	      int low = 0, high = pstr->valid_len, mid;
	      do
		{
		  mid = (high + low) / 2;
		  if (pstr->offsets[mid] > offset)
		    high = mid;
		  else if (pstr->offsets[mid] < offset)
		    low = mid + 1;
		  else
		    break;
		}
	      while (low < high);
	      if (pstr->offsets[mid] < offset)
		++mid;
	      pstr->tip_context = re_string_context_at (pstr, mid - 1,
							eflags);
	      /* This can be quite complicated, so handle specially
		 only the common and easy case where the character with
		 different length representation of lower and upper
		 case is present at or after offset.  */
	      if (pstr->valid_len > offset
		  && mid == offset && pstr->offsets[mid] == offset)
		{
		  memmove (pstr->wcs, pstr->wcs + offset,
			   (pstr->valid_len - offset) * sizeof (wint_t));
		  memmove (pstr->mbs, pstr->mbs + offset, pstr->valid_len - offset);
		  pstr->valid_len -= offset;
		  pstr->valid_raw_len -= offset;
		  for (low = 0; low < pstr->valid_len; low++)
		    pstr->offsets[low] = pstr->offsets[low + offset] - offset;
		}
	      else
		{
		  /* Otherwise, just find out how long the partial multibyte
		     character at offset is and fill it with WEOF/255.  */
		  pstr->len = pstr->raw_len - idx + offset;
		  pstr->stop = pstr->raw_stop - idx + offset;
		  pstr->offsets_needed = 0;
		  while (mid > 0 && pstr->offsets[mid - 1] == offset)
		    --mid;
		  while (mid < pstr->valid_len)
		    if (pstr->wcs[mid] != WEOF)
		      break;
		    else
		      ++mid;
		  if (mid == pstr->valid_len)
		    pstr->valid_len = 0;
		  else
		    {
		      pstr->valid_len = pstr->offsets[mid] - offset;
		      if (pstr->valid_len)
			{
			  for (low = 0; low < pstr->valid_len; ++low)
			    pstr->wcs[low] = WEOF;
			  memset (pstr->mbs, 255, pstr->valid_len);
			}
		    }
		  pstr->valid_raw_len = pstr->valid_len;
		}
	    }
	  else
#endif
	    {
	      pstr->tip_context = re_string_context_at (pstr, offset - 1,
							eflags);
#ifdef RE_ENABLE_I18N
	      if (pstr->mb_cur_max > 1)
		memmove (pstr->wcs, pstr->wcs + offset,
			 (pstr->valid_len - offset) * sizeof (wint_t));
#endif /* RE_ENABLE_I18N */
	      if (BE (pstr->mbs_allocated, 0))
		memmove (pstr->mbs, pstr->mbs + offset,
			 pstr->valid_len - offset);
	      pstr->valid_len -= offset;
	      pstr->valid_raw_len -= offset;
#if DEBUG
	      assert (pstr->valid_len > 0);
#endif
	    }
	}
      else
	{
	  /* No, skip all characters until IDX.  */
	  int prev_valid_len = pstr->valid_len;

#ifdef RE_ENABLE_I18N
	  if (BE (pstr->offsets_needed, 0))
	    {
	      pstr->len = pstr->raw_len - idx + offset;
	      pstr->stop = pstr->raw_stop - idx + offset;
	      pstr->offsets_needed = 0;
	    }
#endif
	  pstr->valid_len = 0;
#ifdef RE_ENABLE_I18N
	  if (pstr->mb_cur_max > 1)
	    {
	      int wcs_idx;
	      wint_t wc = WEOF;

	      if (pstr->is_utf8)
		{
		  const unsigned char *raw, *p, *end;

		  /* Special case UTF-8.  Multi-byte chars start with any
		     byte other than 0x80 - 0xbf.  */
		  raw = pstr->raw_mbs + pstr->raw_mbs_idx;
		  end = raw + (offset - pstr->mb_cur_max);
		  if (end < pstr->raw_mbs)
		    end = pstr->raw_mbs;
		  p = raw + offset - 1;
#ifdef _LIBC
		  /* We know the wchar_t encoding is UCS4, so for the simple
		     case, ASCII characters, skip the conversion step.  */
		  if (isascii (*p) && BE (pstr->trans == NULL, 1))
		    {
		      memset (&pstr->cur_state, '\0', sizeof (mbstate_t));
		      /* pstr->valid_len = 0; */
		      wc = (wchar_t) *p;
		    }
		  else
#endif
		    for (; p >= end; --p)
		      if ((*p & 0xc0) != 0x80)
			{
			  mbstate_t cur_state;
			  wchar_t wc2;
			  int mlen = raw + pstr->len - p;
			  unsigned char buf[6];
			  size_t mbclen;

			  const unsigned char *pp = p;
			  if (BE (pstr->trans != NULL, 0))
			    {
			      int i = mlen < 6 ? mlen : 6;
			      while (--i >= 0)
				buf[i] = pstr->trans[p[i]];
			      pp = buf;
			    }
			  /* XXX Don't use mbrtowc, we know which conversion
			     to use (UTF-8 -> UCS4).  */
			  memset (&cur_state, 0, sizeof (cur_state));
			  mbclen = __mbrtowc (&wc2, (const char *) pp, mlen,
					      &cur_state);
			  if (raw + offset - p <= mbclen
			      && mbclen < (size_t) -2)
			    {
			      memset (&pstr->cur_state, '\0',
				      sizeof (mbstate_t));
			      pstr->valid_len = mbclen - (raw + offset - p);
			      wc = wc2;
			    }
			  break;
			}
		}

	      if (wc == WEOF)
		pstr->valid_len = re_string_skip_chars (pstr, idx, &wc) - idx;
	      if (wc == WEOF)
		pstr->tip_context
		  = re_string_context_at (pstr, prev_valid_len - 1, eflags);
	      else
		pstr->tip_context = ((BE (pstr->word_ops_used != 0, 0)
				      && IS_WIDE_WORD_CHAR (wc))
				     ? CONTEXT_WORD
				     : ((IS_WIDE_NEWLINE (wc)
					 && pstr->newline_anchor)
					? CONTEXT_NEWLINE : 0));
	      if (BE (pstr->valid_len, 0))
		{
		  for (wcs_idx = 0; wcs_idx < pstr->valid_len; ++wcs_idx)
		    pstr->wcs[wcs_idx] = WEOF;
		  if (pstr->mbs_allocated)
		    memset (pstr->mbs, 255, pstr->valid_len);
		}
	      pstr->valid_raw_len = pstr->valid_len;
	    }
	  else
#endif /* RE_ENABLE_I18N */
	    {
	      int c = pstr->raw_mbs[pstr->raw_mbs_idx + offset - 1];
	      pstr->valid_raw_len = 0;
	      if (pstr->trans)
		c = pstr->trans[c];
	      pstr->tip_context = (bitset_contain (pstr->word_char, c)
				   ? CONTEXT_WORD
				   : ((IS_NEWLINE (c) && pstr->newline_anchor)
				      ? CONTEXT_NEWLINE : 0));
	    }
	}
      if (!BE (pstr->mbs_allocated, 0))
	pstr->mbs += offset;
    }
  pstr->raw_mbs_idx = idx;
  pstr->len -= offset;
  pstr->stop -= offset;

  /* Then build the buffers.  */
#ifdef RE_ENABLE_I18N
  if (pstr->mb_cur_max > 1)
    {
      if (pstr->icase)
	{
	  reg_errcode_t ret = build_wcs_upper_buffer (pstr);
	  if (BE (ret != REG_NOERROR, 0))
	    return ret;
	}
      else
	build_wcs_buffer (pstr);
    }
  else
#endif /* RE_ENABLE_I18N */
    if (BE (pstr->mbs_allocated, 0))
      {
	if (pstr->icase)
	  build_upper_buffer (pstr);
	else if (pstr->trans != NULL)
	  re_string_translate_buffer (pstr);
      }
    else
      pstr->valid_len = pstr->len;

  pstr->cur_idx = 0;
  return REG_NOERROR;
}

static unsigned char
internal_function __attribute ((pure))
re_string_peek_byte_case (const re_string_t *pstr, int idx)
{
  int ch, off;

  /* Handle the common (easiest) cases first.  */
  if (BE (!pstr->mbs_allocated, 1))
    return re_string_peek_byte (pstr, idx);

#ifdef RE_ENABLE_I18N
  if (pstr->mb_cur_max > 1
      && ! re_string_is_single_byte_char (pstr, pstr->cur_idx + idx))
    return re_string_peek_byte (pstr, idx);
#endif

  off = pstr->cur_idx + idx;
#ifdef RE_ENABLE_I18N
  if (pstr->offsets_needed)
    off = pstr->offsets[off];
#endif

  ch = pstr->raw_mbs[pstr->raw_mbs_idx + off];

#ifdef RE_ENABLE_I18N
  /* Ensure that e.g. for tr_TR.UTF-8 BACKSLASH DOTLESS SMALL LETTER I
     this function returns CAPITAL LETTER I instead of first byte of
     DOTLESS SMALL LETTER I.  The latter would confuse the parser,
     since peek_byte_case doesn't advance cur_idx in any way.  */
  if (pstr->offsets_needed && !isascii (ch))
    return re_string_peek_byte (pstr, idx);
#endif

  return ch;
}

static unsigned char
internal_function
re_string_fetch_byte_case (re_string_t *pstr)
{
  if (BE (!pstr->mbs_allocated, 1))
    return re_string_fetch_byte (pstr);

#ifdef RE_ENABLE_I18N
  if (pstr->offsets_needed)
    {
      int off, ch;

      /* For tr_TR.UTF-8 [[:islower:]] there is
	 [[: CAPITAL LETTER I WITH DOT lower:]] in mbs.  Skip
	 in that case the whole multi-byte character and return
	 the original letter.  On the other side, with
	 [[: DOTLESS SMALL LETTER I return [[:I, as doing
	 anything else would complicate things too much.  */

      if (!re_string_first_byte (pstr, pstr->cur_idx))
	return re_string_fetch_byte (pstr);

      off = pstr->offsets[pstr->cur_idx];
      ch = pstr->raw_mbs[pstr->raw_mbs_idx + off];

      if (! isascii (ch))
	return re_string_fetch_byte (pstr);

      re_string_skip_bytes (pstr,
			    re_string_char_size_at (pstr, pstr->cur_idx));
      return ch;
    }
#endif

  return pstr->raw_mbs[pstr->raw_mbs_idx + pstr->cur_idx++];
}

static void
internal_function
re_string_destruct (re_string_t *pstr)
{
#ifdef RE_ENABLE_I18N
  re_free (pstr->wcs);
  re_free (pstr->offsets);
#endif /* RE_ENABLE_I18N  */
  if (pstr->mbs_allocated)
    re_free (pstr->mbs);
}

/* Return the context at IDX in INPUT.  */

static unsigned int
internal_function
re_string_context_at (const re_string_t *input, int idx, int eflags)
{
  int c;
  if (BE (idx < 0, 0))
    /* In this case, we use the value stored in input->tip_context,
       since we can't know the character in input->mbs[-1] here.  */
    return input->tip_context;
  if (BE (idx == input->len, 0))
    return ((eflags & REG_NOTEOL) ? CONTEXT_ENDBUF
	    : CONTEXT_NEWLINE | CONTEXT_ENDBUF);
#ifdef RE_ENABLE_I18N
  if (input->mb_cur_max > 1)
    {
      wint_t wc;
      int wc_idx = idx;
      while(input->wcs[wc_idx] == WEOF)
	{
#ifdef DEBUG
	  /* It must not happen.  */
	  assert (wc_idx >= 0);
#endif
	  --wc_idx;
	  if (wc_idx < 0)
	    return input->tip_context;
	}
      wc = input->wcs[wc_idx];
      if (BE (input->word_ops_used != 0, 0) && IS_WIDE_WORD_CHAR (wc))
	return CONTEXT_WORD;
      return (IS_WIDE_NEWLINE (wc) && input->newline_anchor
	      ? CONTEXT_NEWLINE : 0);
    }
  else
#endif
    {
      c = re_string_byte_at (input, idx);
      if (bitset_contain (input->word_char, c))
	return CONTEXT_WORD;
      return IS_NEWLINE (c) && input->newline_anchor ? CONTEXT_NEWLINE : 0;
    }
}

/* Functions for set operation.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_node_set_alloc (re_node_set *set, int size)
{
  set->alloc = size;
  set->nelem = 0;
  set->elems = re_malloc (int, size);
  if (BE (set->elems == NULL, 0))
    return REG_ESPACE;
  return REG_NOERROR;
}

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_node_set_init_1 (re_node_set *set, int elem)
{
  set->alloc = 1;
  set->nelem = 1;
  set->elems = re_malloc (int, 1);
  if (BE (set->elems == NULL, 0))
    {
      set->alloc = set->nelem = 0;
      return REG_ESPACE;
    }
  set->elems[0] = elem;
  return REG_NOERROR;
}

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_node_set_init_2 (re_node_set *set, int elem1, int elem2)
{
  set->alloc = 2;
  set->elems = re_malloc (int, 2);
  if (BE (set->elems == NULL, 0))
    return REG_ESPACE;
  if (elem1 == elem2)
    {
      set->nelem = 1;
      set->elems[0] = elem1;
    }
  else
    {
      set->nelem = 2;
      if (elem1 < elem2)
	{
	  set->elems[0] = elem1;
	  set->elems[1] = elem2;
	}
      else
	{
	  set->elems[0] = elem2;
	  set->elems[1] = elem1;
	}
    }
  return REG_NOERROR;
}

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_node_set_init_copy (re_node_set *dest, const re_node_set *src)
{
  dest->nelem = src->nelem;
  if (src->nelem > 0)
    {
      dest->alloc = dest->nelem;
      dest->elems = re_malloc (int, dest->alloc);
      if (BE (dest->elems == NULL, 0))
	{
	  dest->alloc = dest->nelem = 0;
	  return REG_ESPACE;
	}
      memcpy (dest->elems, src->elems, src->nelem * sizeof (int));
    }
  else
    re_node_set_init_empty (dest);
  return REG_NOERROR;
}

/* Calculate the intersection of the sets SRC1 and SRC2. And merge it to
   DEST. Return value indicate the error code or REG_NOERROR if succeeded.
   Note: We assume dest->elems is NULL, when dest->alloc is 0.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_node_set_add_intersect (re_node_set *dest, const re_node_set *src1,
			   const re_node_set *src2)
{
  int i1, i2, is, id, delta, sbase;
  if (src1->nelem == 0 || src2->nelem == 0)
    return REG_NOERROR;

  /* We need dest->nelem + 2 * elems_in_intersection; this is a
     conservative estimate.  */
  if (src1->nelem + src2->nelem + dest->nelem > dest->alloc)
    {
      int new_alloc = src1->nelem + src2->nelem + dest->alloc;
      int *new_elems = re_realloc (dest->elems, int, new_alloc);
      if (BE (new_elems == NULL, 0))
	return REG_ESPACE;
      dest->elems = new_elems;
      dest->alloc = new_alloc;
    }

  /* Find the items in the intersection of SRC1 and SRC2, and copy
     into the top of DEST those that are not already in DEST itself.  */
  sbase = dest->nelem + src1->nelem + src2->nelem;
  i1 = src1->nelem - 1;
  i2 = src2->nelem - 1;
  id = dest->nelem - 1;
  for (;;)
    {
      if (src1->elems[i1] == src2->elems[i2])
	{
	  /* Try to find the item in DEST.  Maybe we could binary search?  */
	  while (id >= 0 && dest->elems[id] > src1->elems[i1])
	    --id;

	  if (id < 0 || dest->elems[id] != src1->elems[i1])
	    dest->elems[--sbase] = src1->elems[i1];

	  if (--i1 < 0 || --i2 < 0)
	    break;
	}

      /* Lower the highest of the two items.  */
      else if (src1->elems[i1] < src2->elems[i2])
	{
	  if (--i2 < 0)
	    break;
	}
      else
	{
	  if (--i1 < 0)
	    break;
	}
    }

  id = dest->nelem - 1;
  is = dest->nelem + src1->nelem + src2->nelem - 1;
  delta = is - sbase + 1;

  /* Now copy.  When DELTA becomes zero, the remaining
     DEST elements are already in place; this is more or
     less the same loop that is in re_node_set_merge.  */
  dest->nelem += delta;
  if (delta > 0 && id >= 0)
    for (;;)
      {
	if (dest->elems[is] > dest->elems[id])
	  {
	    /* Copy from the top.  */
	    dest->elems[id + delta--] = dest->elems[is--];
	    if (delta == 0)
	      break;
	  }
	else
	  {
	    /* Slide from the bottom.  */
	    dest->elems[id + delta] = dest->elems[id];
	    if (--id < 0)
	      break;
	  }
      }

  /* Copy remaining SRC elements.  */
  memcpy (dest->elems, dest->elems + sbase, delta * sizeof (int));

  return REG_NOERROR;
}

/* Calculate the union set of the sets SRC1 and SRC2. And store it to
   DEST. Return value indicate the error code or REG_NOERROR if succeeded.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_node_set_init_union (re_node_set *dest, const re_node_set *src1,
			const re_node_set *src2)
{
  int i1, i2, id;
  if (src1 != NULL && src1->nelem > 0 && src2 != NULL && src2->nelem > 0)
    {
      dest->alloc = src1->nelem + src2->nelem;
      dest->elems = re_malloc (int, dest->alloc);
      if (BE (dest->elems == NULL, 0))
	return REG_ESPACE;
    }
  else
    {
      if (src1 != NULL && src1->nelem > 0)
	return re_node_set_init_copy (dest, src1);
      else if (src2 != NULL && src2->nelem > 0)
	return re_node_set_init_copy (dest, src2);
      else
	re_node_set_init_empty (dest);
      return REG_NOERROR;
    }
  for (i1 = i2 = id = 0 ; i1 < src1->nelem && i2 < src2->nelem ;)
    {
      if (src1->elems[i1] > src2->elems[i2])
	{
	  dest->elems[id++] = src2->elems[i2++];
	  continue;
	}
      if (src1->elems[i1] == src2->elems[i2])
	++i2;
      dest->elems[id++] = src1->elems[i1++];
    }
  if (i1 < src1->nelem)
    {
      memcpy (dest->elems + id, src1->elems + i1,
	     (src1->nelem - i1) * sizeof (int));
      id += src1->nelem - i1;
    }
  else if (i2 < src2->nelem)
    {
      memcpy (dest->elems + id, src2->elems + i2,
	     (src2->nelem - i2) * sizeof (int));
      id += src2->nelem - i2;
    }
  dest->nelem = id;
  return REG_NOERROR;
}

/* Calculate the union set of the sets DEST and SRC. And store it to
   DEST. Return value indicate the error code or REG_NOERROR if succeeded.  */

static reg_errcode_t
internal_function __attribute_warn_unused_result__
re_node_set_merge (re_node_set *dest, const re_node_set *src)
{
  int is, id, sbase, delta;
  if (src == NULL || src->nelem == 0)
    return REG_NOERROR;
  if (dest->alloc < 2 * src->nelem + dest->nelem)
    {
      int new_alloc = 2 * (src->nelem + dest->alloc);
      int *new_buffer = re_realloc (dest->elems, int, new_alloc);
      if (BE (new_buffer == NULL, 0))
	return REG_ESPACE;
      dest->elems = new_buffer;
      dest->alloc = new_alloc;
    }

  if (BE (dest->nelem == 0, 0))
    {
      dest->nelem = src->nelem;
      memcpy (dest->elems, src->elems, src->nelem * sizeof (int));
      return REG_NOERROR;
    }

  /* Copy into the top of DEST the items of SRC that are not
     found in DEST.  Maybe we could binary search in DEST?  */
  for (sbase = dest->nelem + 2 * src->nelem,
       is = src->nelem - 1, id = dest->nelem - 1; is >= 0 && id >= 0; )
    {
      if (dest->elems[id] == src->elems[is])
	is--, id--;
      else if (dest->elems[id] < src->elems[is])
	dest->elems[--sbase] = src->elems[is--];
      else /* if (dest->elems[id] > src->elems[is]) */
	--id;
    }

  if (is >= 0)
    {
      /* If DEST is exhausted, the remaining items of SRC must be unique.  */
      sbase -= is + 1;
      memcpy (dest->elems + sbase, src->elems, (is + 1) * sizeof (int));
    }

  id = dest->nelem - 1;
  is = dest->nelem + 2 * src->nelem - 1;
  delta = is - sbase + 1;
  if (delta == 0)
    return REG_NOERROR;

  /* Now copy.  When DELTA becomes zero, the remaining
     DEST elements are already in place.  */
  dest->nelem += delta;
  for (;;)
    {
      if (dest->elems[is] > dest->elems[id])
	{
	  /* Copy from the top.  */
	  dest->elems[id + delta--] = dest->elems[is--];
	  if (delta == 0)
	    break;
	}
      else
	{
	  /* Slide from the bottom.  */
	  dest->elems[id + delta] = dest->elems[id];
	  if (--id < 0)
	    {
	      /* Copy remaining SRC elements.  */
	      memcpy (dest->elems, dest->elems + sbase,
		      delta * sizeof (int));
	      break;
	    }
	}
    }

  return REG_NOERROR;
}

/* Insert the new element ELEM to the re_node_set* SET.
   SET should not already have ELEM.
   return -1 if an error is occured, return 1 otherwise.  */

static int
internal_function __attribute_warn_unused_result__
re_node_set_insert (re_node_set *set, int elem)
{
  int idx;
  /* In case the set is empty.  */
  if (set->alloc == 0)
    {
      if (BE (re_node_set_init_1 (set, elem) == REG_NOERROR, 1))
	return 1;
      else
	return -1;
    }

  if (BE (set->nelem, 0) == 0)
    {
      /* We already guaranteed above that set->alloc != 0.  */
      set->elems[0] = elem;
      ++set->nelem;
      return 1;
    }

  /* Realloc if we need.  */
  if (set->alloc == set->nelem)
    {
      int *new_elems;
      set->alloc = set->alloc * 2;
      new_elems = re_realloc (set->elems, int, set->alloc);
      if (BE (new_elems == NULL, 0))
	return -1;
      set->elems = new_elems;
    }

  /* Move the elements which follows the new element.  Test the
     first element separately to skip a check in the inner loop.  */
  if (elem < set->elems[0])
    {
      idx = 0;
      for (idx = set->nelem; idx > 0; idx--)
	set->elems[idx] = set->elems[idx - 1];
    }
  else
    {
      for (idx = set->nelem; set->elems[idx - 1] > elem; idx--)
	set->elems[idx] = set->elems[idx - 1];
    }

  /* Insert the new element.  */
  set->elems[idx] = elem;
  ++set->nelem;
  return 1;
}

/* Insert the new element ELEM to the re_node_set* SET.
   SET should not already have any element greater than or equal to ELEM.
   Return -1 if an error is occured, return 1 otherwise.  */

static int
internal_function __attribute_warn_unused_result__
re_node_set_insert_last (re_node_set *set, int elem)
{
  /* Realloc if we need.  */
  if (set->alloc == set->nelem)
    {
      int *new_elems;
      set->alloc = (set->alloc + 1) * 2;
      new_elems = re_realloc (set->elems, int, set->alloc);
      if (BE (new_elems == NULL, 0))
	return -1;
      set->elems = new_elems;
    }

  /* Insert the new element.  */
  set->elems[set->nelem++] = elem;
  return 1;
}

/* Compare two node sets SET1 and SET2.
   return 1 if SET1 and SET2 are equivalent, return 0 otherwise.  */

static int
internal_function __attribute ((pure))
re_node_set_compare (const re_node_set *set1, const re_node_set *set2)
{
  int i;
  if (set1 == NULL || set2 == NULL || set1->nelem != set2->nelem)
    return 0;
  for (i = set1->nelem ; --i >= 0 ; )
    if (set1->elems[i] != set2->elems[i])
      return 0;
  return 1;
}

/* Return (idx + 1) if SET contains the element ELEM, return 0 otherwise.  */

static int
internal_function __attribute ((pure))
re_node_set_contains (const re_node_set *set, int elem)
{
  unsigned int idx, right, mid;
  if (set->nelem <= 0)
    return 0;

  /* Binary search the element.  */
  idx = 0;
  right = set->nelem - 1;
  while (idx < right)
    {
      mid = (idx + right) / 2;
      if (set->elems[mid] < elem)
	idx = mid + 1;
      else
	right = mid;
    }
  return set->elems[idx] == elem ? idx + 1 : 0;
}

static void
internal_function
re_node_set_remove_at (re_node_set *set, int idx)
{
  if (idx < 0 || idx >= set->nelem)
    return;
  --set->nelem;
  for (; idx < set->nelem; idx++)
    set->elems[idx] = set->elems[idx + 1];
}


/* Add the token TOKEN to dfa->nodes, and return the index of the token.
   Or return -1, if an error will be occured.  */

static int
internal_function
re_dfa_add_node (re_dfa_t *dfa, re_token_t token)
{
  int type = token.type;
  if (BE (dfa->nodes_len >= dfa->nodes_alloc, 0))
    {
      size_t new_nodes_alloc = dfa->nodes_alloc * 2;
      int *new_nexts, *new_indices;
      re_node_set *new_edests, *new_eclosures;
      re_token_t *new_nodes;

      /* Avoid overflows in realloc.  */
      const size_t max_object_size = MAX (sizeof (re_token_t),
					  MAX (sizeof (re_node_set),
					       sizeof (int)));
      if (BE (SIZE_MAX / max_object_size < new_nodes_alloc, 0))
	return -1;

      new_nodes = re_realloc (dfa->nodes, re_token_t, new_nodes_alloc);
      if (BE (new_nodes == NULL, 0))
	return -1;
      dfa->nodes = new_nodes;
      new_nexts = re_realloc (dfa->nexts, int, new_nodes_alloc);
      new_indices = re_realloc (dfa->org_indices, int, new_nodes_alloc);
      new_edests = re_realloc (dfa->edests, re_node_set, new_nodes_alloc);
      new_eclosures = re_realloc (dfa->eclosures, re_node_set, new_nodes_alloc);
      if (BE (new_nexts == NULL || new_indices == NULL
	      || new_edests == NULL || new_eclosures == NULL, 0))
	return -1;
      dfa->nexts = new_nexts;
      dfa->org_indices = new_indices;
      dfa->edests = new_edests;
      dfa->eclosures = new_eclosures;
      dfa->nodes_alloc = new_nodes_alloc;
    }
  dfa->nodes[dfa->nodes_len] = token;
  dfa->nodes[dfa->nodes_len].constraint = 0;
#ifdef RE_ENABLE_I18N
  dfa->nodes[dfa->nodes_len].accept_mb =
    (type == OP_PERIOD && dfa->mb_cur_max > 1) || type == COMPLEX_BRACKET;
#endif
  dfa->nexts[dfa->nodes_len] = -1;
  re_node_set_init_empty (dfa->edests + dfa->nodes_len);
  re_node_set_init_empty (dfa->eclosures + dfa->nodes_len);
  return dfa->nodes_len++;
}

static inline unsigned int
internal_function
calc_state_hash (const re_node_set *nodes, unsigned int context)
{
  unsigned int hash = nodes->nelem + context;
  int i;
  for (i = 0 ; i < nodes->nelem ; i++)
    hash += nodes->elems[i];
  return hash;
}

/* Search for the state whose node_set is equivalent to NODES.
   Return the pointer to the state, if we found it in the DFA.
   Otherwise create the new one and return it.  In case of an error
   return NULL and set the error code in ERR.
   Note: - We assume NULL as the invalid state, then it is possible that
	   return value is NULL and ERR is REG_NOERROR.
	 - We never return non-NULL value in case of any errors, it is for
	   optimization.  */

static re_dfastate_t *
internal_function __attribute_warn_unused_result__
re_acquire_state (reg_errcode_t *err, const re_dfa_t *dfa,
		  const re_node_set *nodes)
{
  unsigned int hash;
  re_dfastate_t *new_state;
  struct re_state_table_entry *spot;
  int i;
  if (BE (nodes->nelem == 0, 0))
    {
      *err = REG_NOERROR;
      return NULL;
    }
  hash = calc_state_hash (nodes, 0);
  spot = dfa->state_table + (hash & dfa->state_hash_mask);

  for (i = 0 ; i < spot->num ; i++)
    {
      re_dfastate_t *state = spot->array[i];
      if (hash != state->hash)
	continue;
      if (re_node_set_compare (&state->nodes, nodes))
	return state;
    }

  /* There are no appropriate state in the dfa, create the new one.  */
  new_state = create_ci_newstate (dfa, nodes, hash);
  if (BE (new_state == NULL, 0))
    *err = REG_ESPACE;

  return new_state;
}

/* Search for the state whose node_set is equivalent to NODES and
   whose context is equivalent to CONTEXT.
   Return the pointer to the state, if we found it in the DFA.
   Otherwise create the new one and return it.  In case of an error
   return NULL and set the error code in ERR.
   Note: - We assume NULL as the invalid state, then it is possible that
	   return value is NULL and ERR is REG_NOERROR.
	 - We never return non-NULL value in case of any errors, it is for
	   optimization.  */

static re_dfastate_t *
internal_function __attribute_warn_unused_result__
re_acquire_state_context (reg_errcode_t *err, const re_dfa_t *dfa,
			  const re_node_set *nodes, unsigned int context)
{
  unsigned int hash;
  re_dfastate_t *new_state;
  struct re_state_table_entry *spot;
  int i;
  if (nodes->nelem == 0)
    {
      *err = REG_NOERROR;
      return NULL;
    }
  hash = calc_state_hash (nodes, context);
  spot = dfa->state_table + (hash & dfa->state_hash_mask);

  for (i = 0 ; i < spot->num ; i++)
    {
      re_dfastate_t *state = spot->array[i];
      if (state->hash == hash
	  && state->context == context
	  && re_node_set_compare (state->entrance_nodes, nodes))
	return state;
    }
  /* There are no appropriate state in `dfa', create the new one.  */
  new_state = create_cd_newstate (dfa, nodes, context, hash);
  if (BE (new_state == NULL, 0))
    *err = REG_ESPACE;

  return new_state;
}

/* Finish initialization of the new state NEWSTATE, and using its hash value
   HASH put in the appropriate bucket of DFA's state table.  Return value
   indicates the error code if failed.  */

static reg_errcode_t
__attribute_warn_unused_result__
register_state (const re_dfa_t *dfa, re_dfastate_t *newstate,
		unsigned int hash)
{
  struct re_state_table_entry *spot;
  reg_errcode_t err;
  int i;

  newstate->hash = hash;
  err = re_node_set_alloc (&newstate->non_eps_nodes, newstate->nodes.nelem);
  if (BE (err != REG_NOERROR, 0))
    return REG_ESPACE;
  for (i = 0; i < newstate->nodes.nelem; i++)
    {
      int elem = newstate->nodes.elems[i];
      if (!IS_EPSILON_NODE (dfa->nodes[elem].type))
	if (re_node_set_insert_last (&newstate->non_eps_nodes, elem) < 0)
	  return REG_ESPACE;
    }

  spot = dfa->state_table + (hash & dfa->state_hash_mask);
  if (BE (spot->alloc <= spot->num, 0))
    {
      int new_alloc = 2 * spot->num + 2;
      re_dfastate_t **new_array = re_realloc (spot->array, re_dfastate_t *,
					      new_alloc);
      if (BE (new_array == NULL, 0))
	return REG_ESPACE;
      spot->array = new_array;
      spot->alloc = new_alloc;
    }
  spot->array[spot->num++] = newstate;
  return REG_NOERROR;
}

static void
free_state (re_dfastate_t *state)
{
  re_node_set_free (&state->non_eps_nodes);
  re_node_set_free (&state->inveclosure);
  if (state->entrance_nodes != &state->nodes)
    {
      re_node_set_free (state->entrance_nodes);
      re_free (state->entrance_nodes);
    }
  re_node_set_free (&state->nodes);
  re_free (state->word_trtable);
  re_free (state->trtable);
  re_free (state);
}

/* Create the new state which is independ of contexts.
   Return the new state if succeeded, otherwise return NULL.  */

static re_dfastate_t *
internal_function __attribute_warn_unused_result__
create_ci_newstate (const re_dfa_t *dfa, const re_node_set *nodes,
		    unsigned int hash)
{
  int i;
  reg_errcode_t err;
  re_dfastate_t *newstate;

  newstate = (re_dfastate_t *) calloc (sizeof (re_dfastate_t), 1);
  if (BE (newstate == NULL, 0))
    return NULL;
  err = re_node_set_init_copy (&newstate->nodes, nodes);
  if (BE (err != REG_NOERROR, 0))
    {
      re_free (newstate);
      return NULL;
    }

  newstate->entrance_nodes = &newstate->nodes;
  for (i = 0 ; i < nodes->nelem ; i++)
    {
      re_token_t *node = dfa->nodes + nodes->elems[i];
      re_token_type_t type = node->type;
      if (type == CHARACTER && !node->constraint)
	continue;
#ifdef RE_ENABLE_I18N
      newstate->accept_mb |= node->accept_mb;
#endif /* RE_ENABLE_I18N */

      /* If the state has the halt node, the state is a halt state.  */
      if (type == END_OF_RE)
	newstate->halt = 1;
      else if (type == OP_BACK_REF)
	newstate->has_backref = 1;
      else if (type == ANCHOR || node->constraint)
	newstate->has_constraint = 1;
    }
  err = register_state (dfa, newstate, hash);
  if (BE (err != REG_NOERROR, 0))
    {
      free_state (newstate);
      newstate = NULL;
    }
  return newstate;
}

/* Create the new state which is depend on the context CONTEXT.
   Return the new state if succeeded, otherwise return NULL.  */

static re_dfastate_t *
internal_function __attribute_warn_unused_result__
create_cd_newstate (const re_dfa_t *dfa, const re_node_set *nodes,
		    unsigned int context, unsigned int hash)
{
  int i, nctx_nodes = 0;
  reg_errcode_t err;
  re_dfastate_t *newstate;

  newstate = (re_dfastate_t *) calloc (sizeof (re_dfastate_t), 1);
  if (BE (newstate == NULL, 0))
    return NULL;
  err = re_node_set_init_copy (&newstate->nodes, nodes);
  if (BE (err != REG_NOERROR, 0))
    {
      re_free (newstate);
      return NULL;
    }

  newstate->context = context;
  newstate->entrance_nodes = &newstate->nodes;

  for (i = 0 ; i < nodes->nelem ; i++)
    {
      re_token_t *node = dfa->nodes + nodes->elems[i];
      re_token_type_t type = node->type;
      unsigned int constraint = node->constraint;

      if (type == CHARACTER && !constraint)
	continue;
#ifdef RE_ENABLE_I18N
      newstate->accept_mb |= node->accept_mb;
#endif /* RE_ENABLE_I18N */

      /* If the state has the halt node, the state is a halt state.  */
      if (type == END_OF_RE)
	newstate->halt = 1;
      else if (type == OP_BACK_REF)
	newstate->has_backref = 1;

      if (constraint)
	{
	  if (newstate->entrance_nodes == &newstate->nodes)
	    {
	      newstate->entrance_nodes = re_malloc (re_node_set, 1);
	      if (BE (newstate->entrance_nodes == NULL, 0))
		{
		  free_state (newstate);
		  return NULL;
		}
	      if (re_node_set_init_copy (newstate->entrance_nodes, nodes)
		  != REG_NOERROR)
		return NULL;
	      nctx_nodes = 0;
	      newstate->has_constraint = 1;
	    }

	  if (NOT_SATISFY_PREV_CONSTRAINT (constraint,context))
	    {
	      re_node_set_remove_at (&newstate->nodes, i - nctx_nodes);
	      ++nctx_nodes;
	    }
	}
    }
  err = register_state (dfa, newstate, hash);
  if (BE (err != REG_NOERROR, 0))
    {
      free_state (newstate);
      newstate = NULL;
    }
  return  newstate;
}
