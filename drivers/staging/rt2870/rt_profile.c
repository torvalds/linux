/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************
 */

#include "rt_config.h"

static void HTParametersHook(
	IN	PRTMP_ADAPTER pAd,
	IN	CHAR		  *pValueStr,
	IN	CHAR		  *pInput);

#define ETH_MAC_ADDR_STR_LEN 17  // in format of xx:xx:xx:xx:xx:xx

// We assume the s1 is a sting, s2 is a memory space with 6 bytes. and content of s1 will be changed.
BOOLEAN rtstrmactohex(char *s1, char *s2)
{
	int i = 0;
	char *ptokS = s1, *ptokE = s1;

	if (strlen(s1) != ETH_MAC_ADDR_STR_LEN)
		return FALSE;

	while((*ptokS) != '\0')
	{
		if((ptokE = strchr(ptokS, ':')) != NULL)
			*ptokE++ = '\0';
		if ((strlen(ptokS) != 2) || (!isxdigit(*ptokS)) || (!isxdigit(*(ptokS+1))))
			break; // fail
		AtoH(ptokS, &s2[i++], 1);
		ptokS = ptokE;
		if (i == 6)
			break; // parsing finished
	}

	return ( i == 6 ? TRUE : FALSE);

}


// we assume the s1 and s2 both are strings.
BOOLEAN rtstrcasecmp(char *s1, char *s2)
{
	char *p1 = s1, *p2 = s2;

	if (strlen(s1) != strlen(s2))
		return FALSE;

	while(*p1 != '\0')
	{
		if((*p1 != *p2) && ((*p1 ^ *p2) != 0x20))
			return FALSE;
		p1++;
		p2++;
	}

	return TRUE;
}

// we assume the s1 (buffer) and s2 (key) both are strings.
char * rtstrstruncasecmp(char * s1, char * s2)
{
	INT l1, l2, i;
	char temp1, temp2;

	l2 = strlen(s2);
	if (!l2)
		return (char *) s1;

	l1 = strlen(s1);

	while (l1 >= l2)
	{
		l1--;

		for(i=0; i<l2; i++)
		{
			temp1 = *(s1+i);
			temp2 = *(s2+i);

			if (('a' <= temp1) && (temp1 <= 'z'))
				temp1 = 'A'+(temp1-'a');
			if (('a' <= temp2) && (temp2 <= 'z'))
				temp2 = 'A'+(temp2-'a');

			if (temp1 != temp2)
				break;
		}

		if (i == l2)
			return (char *) s1;

		s1++;
	}

	return NULL; // not found
}

//add by kathy

 /**
  * strstr - Find the first substring in a %NUL terminated string
  * @s1: The string to be searched
  * @s2: The string to search for
  */
char * rtstrstr(const char * s1,const char * s2)
{
	INT l1, l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *) s1;

	l1 = strlen(s1);

	while (l1 >= l2)
	{
		l1--;
		if (!memcmp(s1,s2,l2))
			return (char *) s1;
		s1++;
	}

	return NULL;
}

/**
 * rstrtok - Split a string into tokens
 * @s: The string to be searched
 * @ct: The characters to search for
 * * WARNING: strtok is deprecated, use strsep instead. However strsep is not compatible with old architecture.
 */
char * __rstrtok;
char * rstrtok(char * s,const char * ct)
{
	char *sbegin, *send;

	sbegin  = s ? s : __rstrtok;
	if (!sbegin)
	{
		return NULL;
	}

	sbegin += strspn(sbegin,ct);
	if (*sbegin == '\0')
	{
		__rstrtok = NULL;
		return( NULL );
	}

	send = strpbrk( sbegin, ct);
	if (send && *send != '\0')
		*send++ = '\0';

	__rstrtok = send;

	return (sbegin);
}

/**
 * delimitcnt - return the count of a given delimiter in a given string.
 * @s: The string to be searched.
 * @ct: The delimiter to search for.
 * Notice : We suppose the delimiter is a single-char string(for example : ";").
 */
INT delimitcnt(char * s,const char * ct)
{
	INT count = 0;
	/* point to the beginning of the line */
	const char *token = s;

	for ( ;; )
	{
		token = strpbrk(token, ct); /* search for delimiters */

        if ( token == NULL )
		{
			/* advanced to the terminating null character */
			break;
		}
		/* skip the delimiter */
	    ++token;

		/*
		 * Print the found text: use len with %.*s to specify field width.
		 */

		/* accumulate delimiter count */
	    ++count;
	}
    return count;
}

/*
  * converts the Internet host address from the standard numbers-and-dots notation
  * into binary data.
  * returns nonzero if the address is valid, zero if not.
  */
int rtinet_aton(const char *cp, unsigned int *addr)
{
	unsigned int 	val;
	int         	base, n;
	char        	c;
	unsigned int    parts[4];
	unsigned int    *pp = parts;

	for (;;)
    {
         /*
          * Collect number up to ``.''.
          * Values are specified as for C:
          *	0x=hex, 0=octal, other=decimal.
          */
         val = 0;
         base = 10;
         if (*cp == '0')
         {
             if (*++cp == 'x' || *cp == 'X')
                 base = 16, cp++;
             else
                 base = 8;
         }
         while ((c = *cp) != '\0')
         {
             if (isdigit((unsigned char) c))
             {
                 val = (val * base) + (c - '0');
                 cp++;
                 continue;
             }
             if (base == 16 && isxdigit((unsigned char) c))
             {
                 val = (val << 4) +
                     (c + 10 - (islower((unsigned char) c) ? 'a' : 'A'));
                 cp++;
                 continue;
             }
             break;
         }
         if (*cp == '.')
         {
             /*
              * Internet format: a.b.c.d a.b.c   (with c treated as 16-bits)
              * a.b     (with b treated as 24 bits)
              */
             if (pp >= parts + 3 || val > 0xff)
                 return 0;
             *pp++ = val, cp++;
         }
         else
             break;
     }

     /*
      * Check for trailing junk.
      */
     while (*cp)
         if (!isspace((unsigned char) *cp++))
             return 0;

     /*
      * Concoct the address according to the number of parts specified.
      */
     n = pp - parts + 1;
     switch (n)
     {

         case 1:         /* a -- 32 bits */
             break;

         case 2:         /* a.b -- 8.24 bits */
             if (val > 0xffffff)
                 return 0;
             val |= parts[0] << 24;
             break;

         case 3:         /* a.b.c -- 8.8.16 bits */
             if (val > 0xffff)
                 return 0;
             val |= (parts[0] << 24) | (parts[1] << 16);
             break;

         case 4:         /* a.b.c.d -- 8.8.8.8 bits */
             if (val > 0xff)
                 return 0;
             val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
             break;
     }

     *addr = htonl(val);
     return 1;

}

/*
    ========================================================================

    Routine Description:
        Find key section for Get key parameter.

    Arguments:
        buffer                      Pointer to the buffer to start find the key section
        section                     the key of the secion to be find

    Return Value:
        NULL                        Fail
        Others                      Success
    ========================================================================
*/
PUCHAR  RTMPFindSection(
    IN  PCHAR   buffer)
{
    CHAR temp_buf[32];
    PUCHAR  ptr;

    strcpy(temp_buf, "Default");

    if((ptr = rtstrstr(buffer, temp_buf)) != NULL)
            return (ptr+strlen("\n"));
        else
            return NULL;
}

/*
    ========================================================================

    Routine Description:
        Get key parameter.

    Arguments:
        key                         Pointer to key string
        dest                        Pointer to destination
        destsize                    The datasize of the destination
        buffer                      Pointer to the buffer to start find the key

    Return Value:
        TRUE                        Success
        FALSE                       Fail

    Note:
        This routine get the value with the matched key (case case-sensitive)
    ========================================================================
*/
INT RTMPGetKeyParameter(
    IN  PCHAR   key,
    OUT PCHAR   dest,
    IN  INT     destsize,
    IN  PCHAR   buffer)
{
    UCHAR *temp_buf1 = NULL;
    UCHAR *temp_buf2 = NULL;
    CHAR *start_ptr;
    CHAR *end_ptr;
    CHAR *ptr;
    CHAR *offset = 0;
    INT  len;

	//temp_buf1 = kmalloc(MAX_PARAM_BUFFER_SIZE, MEM_ALLOC_FLAG);
	os_alloc_mem(NULL, &temp_buf1, MAX_PARAM_BUFFER_SIZE);

	if(temp_buf1 == NULL)
        return (FALSE);

	//temp_buf2 = kmalloc(MAX_PARAM_BUFFER_SIZE, MEM_ALLOC_FLAG);
	os_alloc_mem(NULL, &temp_buf2, MAX_PARAM_BUFFER_SIZE);
	if(temp_buf2 == NULL)
	{
		os_free_mem(NULL, temp_buf1);
        return (FALSE);
	}

    //find section
    if((offset = RTMPFindSection(buffer)) == NULL)
    {
    	os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    strcpy(temp_buf1, "\n");
    strcat(temp_buf1, key);
    strcat(temp_buf1, "=");

    //search key
    if((start_ptr=rtstrstr(offset, temp_buf1))==NULL)
    {
		os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    start_ptr+=strlen("\n");
    if((end_ptr=rtstrstr(start_ptr, "\n"))==NULL)
       end_ptr=start_ptr+strlen(start_ptr);

    if (end_ptr<start_ptr)
    {
		os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    NdisMoveMemory(temp_buf2, start_ptr, end_ptr-start_ptr);
    temp_buf2[end_ptr-start_ptr]='\0';
    len = strlen(temp_buf2);
    strcpy(temp_buf1, temp_buf2);
    if((start_ptr=rtstrstr(temp_buf1, "=")) == NULL)
    {
		os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    strcpy(temp_buf2, start_ptr+1);
    ptr = temp_buf2;
    //trim space or tab
    while(*ptr != 0x00)
    {
        if( (*ptr == ' ') || (*ptr == '\t') )
            ptr++;
        else
           break;
    }

    len = strlen(ptr);
    memset(dest, 0x00, destsize);
    strncpy(dest, ptr, len >= destsize ?  destsize: len);

	os_free_mem(NULL, temp_buf1);
    os_free_mem(NULL, temp_buf2);
    return TRUE;
}

/*
    ========================================================================

    Routine Description:
        Get key parameter.

    Arguments:
        key                         Pointer to key string
        dest                        Pointer to destination
        destsize                    The datasize of the destination
        buffer                      Pointer to the buffer to start find the key

    Return Value:
        TRUE                        Success
        FALSE                       Fail

    Note:
        This routine get the value with the matched key (case case-sensitive).
        It is called for parsing SSID and any key string.
    ========================================================================
*/
INT RTMPGetCriticalParameter(
    IN  PCHAR   key,
    OUT PCHAR   dest,
    IN  INT     destsize,
    IN  PCHAR   buffer)
{
    UCHAR *temp_buf1 = NULL;
    UCHAR *temp_buf2 = NULL;
    CHAR *start_ptr;
    CHAR *end_ptr;
    CHAR *ptr;
    CHAR *offset = 0;
    INT  len;

	//temp_buf1 = kmalloc(MAX_PARAM_BUFFER_SIZE, MEM_ALLOC_FLAG);
	os_alloc_mem(NULL, &temp_buf1, MAX_PARAM_BUFFER_SIZE);

	if(temp_buf1 == NULL)
        return (FALSE);

	//temp_buf2 = kmalloc(MAX_PARAM_BUFFER_SIZE, MEM_ALLOC_FLAG);
	os_alloc_mem(NULL, &temp_buf2, MAX_PARAM_BUFFER_SIZE);
	if(temp_buf2 == NULL)
	{
		os_free_mem(NULL, temp_buf1);
        return (FALSE);
	}

    //find section
    if((offset = RTMPFindSection(buffer)) == NULL)
    {
    	os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    strcpy(temp_buf1, "\n");
    strcat(temp_buf1, key);
    strcat(temp_buf1, "=");

    //search key
    if((start_ptr=rtstrstr(offset, temp_buf1))==NULL)
    {
		os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    start_ptr+=strlen("\n");
    if((end_ptr=rtstrstr(start_ptr, "\n"))==NULL)
       end_ptr=start_ptr+strlen(start_ptr);

    if (end_ptr<start_ptr)
    {
		os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    NdisMoveMemory(temp_buf2, start_ptr, end_ptr-start_ptr);
    temp_buf2[end_ptr-start_ptr]='\0';
    len = strlen(temp_buf2);
    strcpy(temp_buf1, temp_buf2);
    if((start_ptr=rtstrstr(temp_buf1, "=")) == NULL)
    {
		os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    strcpy(temp_buf2, start_ptr+1);
    ptr = temp_buf2;

    //trim tab
    /* We cannot trim space(' ') for SSID and key string. */
    while(*ptr != 0x00)
    {
        //if( (*ptr == ' ') || (*ptr == '\t') )
        if( (*ptr == '\t') )
            ptr++;
        else
           break;
    }

    len = strlen(ptr);
    memset(dest, 0x00, destsize);
    strncpy(dest, ptr, len >= destsize ?  destsize: len);

	os_free_mem(NULL, temp_buf1);
    os_free_mem(NULL, temp_buf2);
    return TRUE;
}

/*
    ========================================================================

    Routine Description:
        Get multiple key parameter.

    Arguments:
        key                         Pointer to key string
        dest                        Pointer to destination
        destsize                    The datasize of the destination
        buffer                      Pointer to the buffer to start find the key

    Return Value:
        TRUE                        Success
        FALSE                       Fail

    Note:
        This routine get the value with the matched key (case case-sensitive)
    ========================================================================
*/
INT RTMPGetKeyParameterWithOffset(
    IN  PCHAR   key,
    OUT PCHAR   dest,
    OUT	USHORT	*end_offset,
    IN  INT     destsize,
    IN  PCHAR   buffer,
    IN	BOOLEAN	bTrimSpace)
{
    UCHAR *temp_buf1 = NULL;
    UCHAR *temp_buf2 = NULL;
    CHAR *start_ptr;
    CHAR *end_ptr;
    CHAR *ptr;
    CHAR *offset = 0;
    INT  len;

	if (*end_offset >= MAX_INI_BUFFER_SIZE)
		return (FALSE);

	os_alloc_mem(NULL, &temp_buf1, MAX_PARAM_BUFFER_SIZE);

	if(temp_buf1 == NULL)
        return (FALSE);

	os_alloc_mem(NULL, &temp_buf2, MAX_PARAM_BUFFER_SIZE);
	if(temp_buf2 == NULL)
	{
		os_free_mem(NULL, temp_buf1);
        return (FALSE);
	}

    //find section
	if(*end_offset == 0)
    {
		if ((offset = RTMPFindSection(buffer)) == NULL)
		{
			os_free_mem(NULL, temp_buf1);
	    	os_free_mem(NULL, temp_buf2);
    	    return (FALSE);
		}
    }
	else
		offset = buffer + (*end_offset);

    strcpy(temp_buf1, "\n");
    strcat(temp_buf1, key);
    strcat(temp_buf1, "=");

    //search key
    if((start_ptr=rtstrstr(offset, temp_buf1))==NULL)
    {
		os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    start_ptr+=strlen("\n");
    if((end_ptr=rtstrstr(start_ptr, "\n"))==NULL)
       end_ptr=start_ptr+strlen(start_ptr);

    if (end_ptr<start_ptr)
    {
		os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

	*end_offset = end_ptr - buffer;

    NdisMoveMemory(temp_buf2, start_ptr, end_ptr-start_ptr);
    temp_buf2[end_ptr-start_ptr]='\0';
    len = strlen(temp_buf2);
    strcpy(temp_buf1, temp_buf2);
    if((start_ptr=rtstrstr(temp_buf1, "=")) == NULL)
    {
		os_free_mem(NULL, temp_buf1);
    	os_free_mem(NULL, temp_buf2);
        return (FALSE);
    }

    strcpy(temp_buf2, start_ptr+1);
    ptr = temp_buf2;
    //trim space or tab
    while(*ptr != 0x00)
    {
        if((bTrimSpace && (*ptr == ' ')) || (*ptr == '\t') )
            ptr++;
        else
           break;
    }

    len = strlen(ptr);
    memset(dest, 0x00, destsize);
    strncpy(dest, ptr, len >= destsize ?  destsize: len);

	os_free_mem(NULL, temp_buf1);
    os_free_mem(NULL, temp_buf2);
    return TRUE;
}


static int rtmp_parse_key_buffer_from_file(IN  PRTMP_ADAPTER pAd,IN  char *buffer,IN  ULONG KeyType,IN  INT BSSIdx,IN  INT KeyIdx)
{
	PUCHAR		keybuff;
	INT			i = BSSIdx, idx = KeyIdx;
	ULONG		KeyLen;
	UCHAR		CipherAlg = CIPHER_WEP64;

	keybuff = buffer;
	KeyLen = strlen(keybuff);

	if (KeyType == 1)
	{//Ascii
		if( (KeyLen == 5) || (KeyLen == 13))
		{
			pAd->SharedKey[i][idx].KeyLen = KeyLen;
			NdisMoveMemory(pAd->SharedKey[i][idx].Key, keybuff, KeyLen);
			if (KeyLen == 5)
				CipherAlg = CIPHER_WEP64;
			else
				CipherAlg = CIPHER_WEP128;
			pAd->SharedKey[i][idx].CipherAlg = CipherAlg;

			DBGPRINT(RT_DEBUG_TRACE, ("I/F(ra%d) Key%dStr=%s and type=%s\n", i, idx+1, keybuff, (KeyType == 0) ? "Hex":"Ascii"));
			return 1;
		}
		else
		{//Invalid key length
			DBGPRINT(RT_DEBUG_ERROR, ("Key%dStr is Invalid key length! KeyLen = %ld!\n", idx+1, KeyLen));
			return 0;
		}
	}
	else
	{//Hex type
		if( (KeyLen == 10) || (KeyLen == 26))
		{
			pAd->SharedKey[i][idx].KeyLen = KeyLen / 2;
			AtoH(keybuff, pAd->SharedKey[i][idx].Key, KeyLen / 2);
			if (KeyLen == 10)
				CipherAlg = CIPHER_WEP64;
			else
				CipherAlg = CIPHER_WEP128;
			pAd->SharedKey[i][idx].CipherAlg = CipherAlg;

			DBGPRINT(RT_DEBUG_TRACE, ("I/F(ra%d) Key%dStr=%s and type=%s\n", i, idx+1, keybuff, (KeyType == 0) ? "Hex":"Ascii"));
			return 1;
		}
		else
		{//Invalid key length
			DBGPRINT(RT_DEBUG_ERROR, ("I/F(ra%d) Key%dStr is Invalid key length! KeyLen = %ld!\n", i, idx+1, KeyLen));
			return 0;
		}
	}
}
static void rtmp_read_key_parms_from_file(IN  PRTMP_ADAPTER pAd, char *tmpbuf, char *buffer)
{
	char		tok_str[16];
	PUCHAR		macptr;
	INT			i = 0, idx;
	ULONG		KeyType[MAX_MBSSID_NUM];
	ULONG		KeyIdx;

	NdisZeroMemory(KeyType, MAX_MBSSID_NUM);

	//DefaultKeyID
	if(RTMPGetKeyParameter("DefaultKeyID", tmpbuf, 25, buffer))
	{
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			KeyIdx = simple_strtol(tmpbuf, 0, 10);
			if((KeyIdx >= 1 ) && (KeyIdx <= 4))
				pAd->StaCfg.DefaultKeyId = (UCHAR) (KeyIdx - 1);
			else
				pAd->StaCfg.DefaultKeyId = 0;

			DBGPRINT(RT_DEBUG_TRACE, ("DefaultKeyID(0~3)=%d\n", pAd->StaCfg.DefaultKeyId));
		}
	}


	for (idx = 0; idx < 4; idx++)
	{
		sprintf(tok_str, "Key%dType", idx + 1);
		//Key1Type
		if (RTMPGetKeyParameter(tok_str, tmpbuf, 128, buffer))
		{
		    for (i = 0, macptr = rstrtok(tmpbuf,";"); macptr; macptr = rstrtok(NULL,";"), i++)
		    {
			    KeyType[i] = simple_strtol(macptr, 0, 10);
		    }

			IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			{
				sprintf(tok_str, "Key%dStr", idx + 1);
				if (RTMPGetCriticalParameter(tok_str, tmpbuf, 128, buffer))
				{
					rtmp_parse_key_buffer_from_file(pAd, tmpbuf, KeyType[BSS0], BSS0, idx);
				}
			}
		}
	}
}

static void rtmp_read_sta_wmm_parms_from_file(IN  PRTMP_ADAPTER pAd, char *tmpbuf, char *buffer)
{
	PUCHAR					macptr;
	INT						i=0;
	BOOLEAN					bWmmEnable = FALSE;

	//WmmCapable
	if(RTMPGetKeyParameter("WmmCapable", tmpbuf, 32, buffer))
	{
		if(simple_strtol(tmpbuf, 0, 10) != 0) //Enable
		{
			pAd->CommonCfg.bWmmCapable = TRUE;
			bWmmEnable = TRUE;
		}
		else //Disable
		{
			pAd->CommonCfg.bWmmCapable = FALSE;
		}

		DBGPRINT(RT_DEBUG_TRACE, ("WmmCapable=%d\n", pAd->CommonCfg.bWmmCapable));
	}

	//AckPolicy for AC_BK, AC_BE, AC_VI, AC_VO
	if(RTMPGetKeyParameter("AckPolicy", tmpbuf, 32, buffer))
	{
		for (i = 0, macptr = rstrtok(tmpbuf,";"); macptr; macptr = rstrtok(NULL,";"), i++)
		{
			pAd->CommonCfg.AckPolicy[i] = (UCHAR)simple_strtol(macptr, 0, 10);

			DBGPRINT(RT_DEBUG_TRACE, ("AckPolicy[%d]=%d\n", i, pAd->CommonCfg.AckPolicy[i]));
		}
	}

	if (bWmmEnable)
	{
		//APSDCapable
		if(RTMPGetKeyParameter("APSDCapable", tmpbuf, 10, buffer))
		{
			if(simple_strtol(tmpbuf, 0, 10) != 0)  //Enable
				pAd->CommonCfg.bAPSDCapable = TRUE;
			else
				pAd->CommonCfg.bAPSDCapable = FALSE;

			DBGPRINT(RT_DEBUG_TRACE, ("APSDCapable=%d\n", pAd->CommonCfg.bAPSDCapable));
		}

		//APSDAC for AC_BE, AC_BK, AC_VI, AC_VO
		if(RTMPGetKeyParameter("APSDAC", tmpbuf, 32, buffer))
		{
			BOOLEAN apsd_ac[4];

			for (i = 0, macptr = rstrtok(tmpbuf,";"); macptr; macptr = rstrtok(NULL,";"), i++)
			{
				apsd_ac[i] = (BOOLEAN)simple_strtol(macptr, 0, 10);

				DBGPRINT(RT_DEBUG_TRACE, ("APSDAC%d  %d\n", i,  apsd_ac[i]));
			}

			pAd->CommonCfg.bAPSDAC_BE = apsd_ac[0];
			pAd->CommonCfg.bAPSDAC_BK = apsd_ac[1];
			pAd->CommonCfg.bAPSDAC_VI = apsd_ac[2];
			pAd->CommonCfg.bAPSDAC_VO = apsd_ac[3];
		}
	}

}

NDIS_STATUS	RTMPReadParametersHook(
	IN	PRTMP_ADAPTER pAd)
{
	PUCHAR					src = NULL;
	struct file				*srcf;
	INT 					retval, orgfsuid, orgfsgid;
   	mm_segment_t			orgfs;
	CHAR					*buffer;
	CHAR					*tmpbuf;
	ULONG					RtsThresh;
	ULONG					FragThresh;
	UCHAR	                keyMaterial[40];

	PUCHAR					macptr;
	INT						i = 0;

	buffer = kmalloc(MAX_INI_BUFFER_SIZE, MEM_ALLOC_FLAG);
	if(buffer == NULL)
        return NDIS_STATUS_FAILURE;

	tmpbuf = kmalloc(MAX_PARAM_BUFFER_SIZE, MEM_ALLOC_FLAG);
	if(tmpbuf == NULL)
	{
		kfree(buffer);
        return NDIS_STATUS_FAILURE;
	}

	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		src = STA_PROFILE_PATH;

	// Save uid and gid used for filesystem access.
	// Set user and group to 0 (root)
	orgfsuid = current_fsuid();
	orgfsgid = current_fsgid();
	/* Hm, can't really do this nicely anymore, so rely on these files
	 * being set to the proper permission to read them... */
	/* current->cred->fsuid = current->cred->fsgid = 0; */
    orgfs = get_fs();
    set_fs(KERNEL_DS);

	if (src && *src)
	{
		srcf = filp_open(src, O_RDONLY, 0);
		if (IS_ERR(srcf))
		{
			DBGPRINT(RT_DEBUG_ERROR, ("--> Error %ld opening %s\n", -PTR_ERR(srcf),src));
		}
		else
		{
			// The object must have a read method
			if (srcf->f_op && srcf->f_op->read)
			{
				memset(buffer, 0x00, MAX_INI_BUFFER_SIZE);
				retval=srcf->f_op->read(srcf, buffer, MAX_INI_BUFFER_SIZE, &srcf->f_pos);
				if (retval < 0)
				{
					DBGPRINT(RT_DEBUG_TRACE, ("--> Read %s error %d\n", src, -retval));
				}
				else
				{
					// set file parameter to portcfg
					//CountryRegion
					if(RTMPGetKeyParameter("CountryRegion", tmpbuf, 25, buffer))
					{
						pAd->CommonCfg.CountryRegion = (UCHAR) simple_strtol(tmpbuf, 0, 10);
						DBGPRINT(RT_DEBUG_TRACE, ("CountryRegion=%d\n", pAd->CommonCfg.CountryRegion));
					}
					//CountryRegionABand
					if(RTMPGetKeyParameter("CountryRegionABand", tmpbuf, 25, buffer))
					{
						pAd->CommonCfg.CountryRegionForABand= (UCHAR) simple_strtol(tmpbuf, 0, 10);
						DBGPRINT(RT_DEBUG_TRACE, ("CountryRegionABand=%d\n", pAd->CommonCfg.CountryRegionForABand));
					}
					//CountryCode
					if(RTMPGetKeyParameter("CountryCode", tmpbuf, 25, buffer))
					{
						NdisMoveMemory(pAd->CommonCfg.CountryCode, tmpbuf , 2);

						if (strlen(pAd->CommonCfg.CountryCode) != 0)
						{
							pAd->CommonCfg.bCountryFlag = TRUE;
						}
						DBGPRINT(RT_DEBUG_TRACE, ("CountryCode=%s\n", pAd->CommonCfg.CountryCode));
					}
					//ChannelGeography
					if(RTMPGetKeyParameter("ChannelGeography", tmpbuf, 25, buffer))
					{
						UCHAR Geography = (UCHAR) simple_strtol(tmpbuf, 0, 10);
						if (Geography <= BOTH)
						{
							pAd->CommonCfg.Geography = Geography;
							pAd->CommonCfg.CountryCode[2] =
								(pAd->CommonCfg.Geography == BOTH) ? ' ' : ((pAd->CommonCfg.Geography == IDOR) ? 'I' : 'O');
							DBGPRINT(RT_DEBUG_TRACE, ("ChannelGeography=%d\n", pAd->CommonCfg.Geography));
						}
					}
					else
					{
						pAd->CommonCfg.Geography = BOTH;
						pAd->CommonCfg.CountryCode[2] = ' ';
					}

					IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
					{
						//SSID
						if (RTMPGetCriticalParameter("SSID", tmpbuf, 256, buffer))
						{
							if (strlen(tmpbuf) <= 32)
							{
			 					pAd->CommonCfg.SsidLen = (UCHAR) strlen(tmpbuf);
								NdisZeroMemory(pAd->CommonCfg.Ssid, NDIS_802_11_LENGTH_SSID);
								NdisMoveMemory(pAd->CommonCfg.Ssid, tmpbuf, pAd->CommonCfg.SsidLen);
								pAd->MlmeAux.AutoReconnectSsidLen = pAd->CommonCfg.SsidLen;
								NdisZeroMemory(pAd->MlmeAux.AutoReconnectSsid, NDIS_802_11_LENGTH_SSID);
								NdisMoveMemory(pAd->MlmeAux.AutoReconnectSsid, tmpbuf, pAd->MlmeAux.AutoReconnectSsidLen);
								pAd->MlmeAux.SsidLen = pAd->CommonCfg.SsidLen;
								NdisZeroMemory(pAd->MlmeAux.Ssid, NDIS_802_11_LENGTH_SSID);
								NdisMoveMemory(pAd->MlmeAux.Ssid, tmpbuf, pAd->MlmeAux.SsidLen);
								DBGPRINT(RT_DEBUG_TRACE, ("%s::(SSID=%s)\n", __func__, tmpbuf));
							}
						}
					}

					IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
					{
						//NetworkType
						if (RTMPGetKeyParameter("NetworkType", tmpbuf, 25, buffer))
						{
							pAd->bConfigChanged = TRUE;
							if (strcmp(tmpbuf, "Adhoc") == 0)
								pAd->StaCfg.BssType = BSS_ADHOC;
							else //Default Infrastructure mode
								pAd->StaCfg.BssType = BSS_INFRA;
							// Reset Ralink supplicant to not use, it will be set to start when UI set PMK key
							pAd->StaCfg.WpaState = SS_NOTUSE;
							DBGPRINT(RT_DEBUG_TRACE, ("%s::(NetworkType=%d)\n", __func__, pAd->StaCfg.BssType));
						}
					}

					//Channel
					if(RTMPGetKeyParameter("Channel", tmpbuf, 10, buffer))
					{
						pAd->CommonCfg.Channel = (UCHAR) simple_strtol(tmpbuf, 0, 10);
						DBGPRINT(RT_DEBUG_TRACE, ("Channel=%d\n", pAd->CommonCfg.Channel));
					}
					//WirelessMode
					if(RTMPGetKeyParameter("WirelessMode", tmpbuf, 10, buffer))
					{
						int value  = 0, maxPhyMode = PHY_11G;

						maxPhyMode = PHY_11N_5G;

						value = simple_strtol(tmpbuf, 0, 10);

						if (value <= maxPhyMode)
						{
							pAd->CommonCfg.PhyMode = value;
						}
						DBGPRINT(RT_DEBUG_TRACE, ("PhyMode=%d\n", pAd->CommonCfg.PhyMode));
					}
                    //BasicRate
					if(RTMPGetKeyParameter("BasicRate", tmpbuf, 10, buffer))
					{
						pAd->CommonCfg.BasicRateBitmap = (ULONG) simple_strtol(tmpbuf, 0, 10);
						DBGPRINT(RT_DEBUG_TRACE, ("BasicRate=%ld\n", pAd->CommonCfg.BasicRateBitmap));
					}
					//BeaconPeriod
					if(RTMPGetKeyParameter("BeaconPeriod", tmpbuf, 10, buffer))
					{
						pAd->CommonCfg.BeaconPeriod = (USHORT) simple_strtol(tmpbuf, 0, 10);
						DBGPRINT(RT_DEBUG_TRACE, ("BeaconPeriod=%d\n", pAd->CommonCfg.BeaconPeriod));
					}
                    //TxPower
					if(RTMPGetKeyParameter("TxPower", tmpbuf, 10, buffer))
					{
						pAd->CommonCfg.TxPowerPercentage = (ULONG) simple_strtol(tmpbuf, 0, 10);

						IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
							pAd->CommonCfg.TxPowerDefault = pAd->CommonCfg.TxPowerPercentage;

						DBGPRINT(RT_DEBUG_TRACE, ("TxPower=%ld\n", pAd->CommonCfg.TxPowerPercentage));
					}
					//BGProtection
					if(RTMPGetKeyParameter("BGProtection", tmpbuf, 10, buffer))
					{
						switch (simple_strtol(tmpbuf, 0, 10))
						{
							case 1: //Always On
								pAd->CommonCfg.UseBGProtection = 1;
								break;
							case 2: //Always OFF
								pAd->CommonCfg.UseBGProtection = 2;
								break;
							case 0: //AUTO
							default:
								pAd->CommonCfg.UseBGProtection = 0;
								break;
						}
						DBGPRINT(RT_DEBUG_TRACE, ("BGProtection=%ld\n", pAd->CommonCfg.UseBGProtection));
					}
					//OLBCDetection
					if(RTMPGetKeyParameter("DisableOLBC", tmpbuf, 10, buffer))
					{
						switch (simple_strtol(tmpbuf, 0, 10))
						{
							case 1: //disable OLBC Detection
								pAd->CommonCfg.DisableOLBCDetect = 1;
								break;
							case 0: //enable OLBC Detection
								pAd->CommonCfg.DisableOLBCDetect = 0;
								break;
							default:
								pAd->CommonCfg.DisableOLBCDetect= 0;
								break;
						}
						DBGPRINT(RT_DEBUG_TRACE, ("OLBCDetection=%ld\n", pAd->CommonCfg.DisableOLBCDetect));
					}
					//TxPreamble
					if(RTMPGetKeyParameter("TxPreamble", tmpbuf, 10, buffer))
					{
						switch (simple_strtol(tmpbuf, 0, 10))
						{
							case Rt802_11PreambleShort:
								pAd->CommonCfg.TxPreamble = Rt802_11PreambleShort;
								break;
							case Rt802_11PreambleLong:
							default:
								pAd->CommonCfg.TxPreamble = Rt802_11PreambleLong;
								break;
						}
						DBGPRINT(RT_DEBUG_TRACE, ("TxPreamble=%ld\n", pAd->CommonCfg.TxPreamble));
					}
					//RTSThreshold
					if(RTMPGetKeyParameter("RTSThreshold", tmpbuf, 10, buffer))
					{
						RtsThresh = simple_strtol(tmpbuf, 0, 10);
						if( (RtsThresh >= 1) && (RtsThresh <= MAX_RTS_THRESHOLD) )
							pAd->CommonCfg.RtsThreshold  = (USHORT)RtsThresh;
						else
							pAd->CommonCfg.RtsThreshold = MAX_RTS_THRESHOLD;

						DBGPRINT(RT_DEBUG_TRACE, ("RTSThreshold=%d\n", pAd->CommonCfg.RtsThreshold));
					}
					//FragThreshold
					if(RTMPGetKeyParameter("FragThreshold", tmpbuf, 10, buffer))
					{
						FragThresh = simple_strtol(tmpbuf, 0, 10);
						pAd->CommonCfg.bUseZeroToDisableFragment = FALSE;

						if (FragThresh > MAX_FRAG_THRESHOLD || FragThresh < MIN_FRAG_THRESHOLD)
						{ //illegal FragThresh so we set it to default
							pAd->CommonCfg.FragmentThreshold = MAX_FRAG_THRESHOLD;
							pAd->CommonCfg.bUseZeroToDisableFragment = TRUE;
						}
						else if (FragThresh % 2 == 1)
						{
							// The length of each fragment shall always be an even number of octets, except for the last fragment
							// of an MSDU or MMPDU, which may be either an even or an odd number of octets.
							pAd->CommonCfg.FragmentThreshold = (USHORT)(FragThresh - 1);
						}
						else
						{
							pAd->CommonCfg.FragmentThreshold = (USHORT)FragThresh;
						}
						//pAd->CommonCfg.AllowFragSize = (pAd->CommonCfg.FragmentThreshold) - LENGTH_802_11 - LENGTH_CRC;
						DBGPRINT(RT_DEBUG_TRACE, ("FragThreshold=%d\n", pAd->CommonCfg.FragmentThreshold));
					}
					//TxBurst
					if(RTMPGetKeyParameter("TxBurst", tmpbuf, 10, buffer))
					{
//#ifdef WIFI_TEST
//						pAd->CommonCfg.bEnableTxBurst = FALSE;
//#else
						if(simple_strtol(tmpbuf, 0, 10) != 0)  //Enable
							pAd->CommonCfg.bEnableTxBurst = TRUE;
						else //Disable
							pAd->CommonCfg.bEnableTxBurst = FALSE;
//#endif
						DBGPRINT(RT_DEBUG_TRACE, ("TxBurst=%d\n", pAd->CommonCfg.bEnableTxBurst));
					}

#ifdef AGGREGATION_SUPPORT
					//PktAggregate
					if(RTMPGetKeyParameter("PktAggregate", tmpbuf, 10, buffer))
					{
						if(simple_strtol(tmpbuf, 0, 10) != 0)  //Enable
							pAd->CommonCfg.bAggregationCapable = TRUE;
						else //Disable
							pAd->CommonCfg.bAggregationCapable = FALSE;
#ifdef PIGGYBACK_SUPPORT
						pAd->CommonCfg.bPiggyBackCapable = pAd->CommonCfg.bAggregationCapable;
#endif // PIGGYBACK_SUPPORT //
						DBGPRINT(RT_DEBUG_TRACE, ("PktAggregate=%d\n", pAd->CommonCfg.bAggregationCapable));
					}
#else
					pAd->CommonCfg.bAggregationCapable = FALSE;
					pAd->CommonCfg.bPiggyBackCapable = FALSE;
#endif // AGGREGATION_SUPPORT //

					// WmmCapable
					IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
						rtmp_read_sta_wmm_parms_from_file(pAd, tmpbuf, buffer);

					//ShortSlot
					if(RTMPGetKeyParameter("ShortSlot", tmpbuf, 10, buffer))
					{
						if(simple_strtol(tmpbuf, 0, 10) != 0)  //Enable
							pAd->CommonCfg.bUseShortSlotTime = TRUE;
						else //Disable
							pAd->CommonCfg.bUseShortSlotTime = FALSE;

						DBGPRINT(RT_DEBUG_TRACE, ("ShortSlot=%d\n", pAd->CommonCfg.bUseShortSlotTime));
					}
					//IEEE80211H
					if(RTMPGetKeyParameter("IEEE80211H", tmpbuf, 10, buffer))
					{
					    for (i = 0, macptr = rstrtok(tmpbuf,";"); macptr; macptr = rstrtok(NULL,";"), i++)
					    {
    						if(simple_strtol(macptr, 0, 10) != 0)  //Enable
    							pAd->CommonCfg.bIEEE80211H = TRUE;
    						else //Disable
    							pAd->CommonCfg.bIEEE80211H = FALSE;

    						DBGPRINT(RT_DEBUG_TRACE, ("IEEE80211H=%d\n", pAd->CommonCfg.bIEEE80211H));
					    }
					}
					//CSPeriod
					if(RTMPGetKeyParameter("CSPeriod", tmpbuf, 10, buffer))
					{
					    if(simple_strtol(tmpbuf, 0, 10) != 0)
							pAd->CommonCfg.RadarDetect.CSPeriod = simple_strtol(tmpbuf, 0, 10);
						else
							pAd->CommonCfg.RadarDetect.CSPeriod = 0;

   						DBGPRINT(RT_DEBUG_TRACE, ("CSPeriod=%d\n", pAd->CommonCfg.RadarDetect.CSPeriod));
					}

					//RDRegion
					if(RTMPGetKeyParameter("RDRegion", tmpbuf, 128, buffer))
					{
						if ((strncmp(tmpbuf, "JAP_W53", 7) == 0) || (strncmp(tmpbuf, "jap_w53", 7) == 0))
						{
							pAd->CommonCfg.RadarDetect.RDDurRegion = JAP_W53;
							pAd->CommonCfg.RadarDetect.DfsSessionTime = 15;
						}
						else if ((strncmp(tmpbuf, "JAP_W56", 7) == 0) || (strncmp(tmpbuf, "jap_w56", 7) == 0))
						{
							pAd->CommonCfg.RadarDetect.RDDurRegion = JAP_W56;
							pAd->CommonCfg.RadarDetect.DfsSessionTime = 13;
						}
						else if ((strncmp(tmpbuf, "JAP", 3) == 0) || (strncmp(tmpbuf, "jap", 3) == 0))
						{
							pAd->CommonCfg.RadarDetect.RDDurRegion = JAP;
							pAd->CommonCfg.RadarDetect.DfsSessionTime = 5;
						}
						else  if ((strncmp(tmpbuf, "FCC", 3) == 0) || (strncmp(tmpbuf, "fcc", 3) == 0))
						{
							pAd->CommonCfg.RadarDetect.RDDurRegion = FCC;
							pAd->CommonCfg.RadarDetect.DfsSessionTime = 5;
						}
						else if ((strncmp(tmpbuf, "CE", 2) == 0) || (strncmp(tmpbuf, "ce", 2) == 0))
						{
							pAd->CommonCfg.RadarDetect.RDDurRegion = CE;
							pAd->CommonCfg.RadarDetect.DfsSessionTime = 13;
						}
						else
						{
							pAd->CommonCfg.RadarDetect.RDDurRegion = CE;
							pAd->CommonCfg.RadarDetect.DfsSessionTime = 13;
						}

						DBGPRINT(RT_DEBUG_TRACE, ("RDRegion=%d\n", pAd->CommonCfg.RadarDetect.RDDurRegion));
					}
					else
					{
						pAd->CommonCfg.RadarDetect.RDDurRegion = CE;
						pAd->CommonCfg.RadarDetect.DfsSessionTime = 13;
					}

					//WirelessEvent
					if(RTMPGetKeyParameter("WirelessEvent", tmpbuf, 10, buffer))
					{
#if WIRELESS_EXT >= 15
					    if(simple_strtol(tmpbuf, 0, 10) != 0)
							pAd->CommonCfg.bWirelessEvent = simple_strtol(tmpbuf, 0, 10);
						else
							pAd->CommonCfg.bWirelessEvent = 0;	// disable
#else
						pAd->CommonCfg.bWirelessEvent = 0;	// disable
#endif
   						DBGPRINT(RT_DEBUG_TRACE, ("WirelessEvent=%d\n", pAd->CommonCfg.bWirelessEvent));
					}
					if(RTMPGetKeyParameter("WiFiTest", tmpbuf, 10, buffer))
					{
					    if(simple_strtol(tmpbuf, 0, 10) != 0)
							pAd->CommonCfg.bWiFiTest= simple_strtol(tmpbuf, 0, 10);
						else
							pAd->CommonCfg.bWiFiTest = 0;	// disable

   						DBGPRINT(RT_DEBUG_TRACE, ("WiFiTest=%d\n", pAd->CommonCfg.bWiFiTest));
					}
					//AuthMode
					if(RTMPGetKeyParameter("AuthMode", tmpbuf, 128, buffer))
					{
						IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
						{
							if ((strcmp(tmpbuf, "WEPAUTO") == 0) || (strcmp(tmpbuf, "wepauto") == 0))
							    pAd->StaCfg.AuthMode = Ndis802_11AuthModeAutoSwitch;
							else if ((strcmp(tmpbuf, "SHARED") == 0) || (strcmp(tmpbuf, "shared") == 0))
							    pAd->StaCfg.AuthMode = Ndis802_11AuthModeShared;
							else if ((strcmp(tmpbuf, "WPAPSK") == 0) || (strcmp(tmpbuf, "wpapsk") == 0))
							    pAd->StaCfg.AuthMode = Ndis802_11AuthModeWPAPSK;
							else if ((strcmp(tmpbuf, "WPANONE") == 0) || (strcmp(tmpbuf, "wpanone") == 0))
							    pAd->StaCfg.AuthMode = Ndis802_11AuthModeWPANone;
							else if ((strcmp(tmpbuf, "WPA2PSK") == 0) || (strcmp(tmpbuf, "wpa2psk") == 0))
							    pAd->StaCfg.AuthMode = Ndis802_11AuthModeWPA2PSK;
							else if ((strcmp(tmpbuf, "WPA") == 0) || (strcmp(tmpbuf, "wpa") == 0))
							    pAd->StaCfg.AuthMode = Ndis802_11AuthModeWPA;
							else if ((strcmp(tmpbuf, "WPA2") == 0) || (strcmp(tmpbuf, "wpa2") == 0))
							    pAd->StaCfg.AuthMode = Ndis802_11AuthModeWPA2;
				                        else
				                            pAd->StaCfg.AuthMode = Ndis802_11AuthModeOpen;

				                        pAd->StaCfg.PortSecured = WPA_802_1X_PORT_NOT_SECURED;

							DBGPRINT(RT_DEBUG_TRACE, ("%s::(EncrypType=%d)\n", __func__, pAd->StaCfg.WepStatus));
						}
					}
					//EncrypType
					if(RTMPGetKeyParameter("EncrypType", tmpbuf, 128, buffer))
					{
						IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
						{
							if ((strcmp(tmpbuf, "WEP") == 0) || (strcmp(tmpbuf, "wep") == 0))
								pAd->StaCfg.WepStatus	= Ndis802_11WEPEnabled;
							else if ((strcmp(tmpbuf, "TKIP") == 0) || (strcmp(tmpbuf, "tkip") == 0))
								pAd->StaCfg.WepStatus	= Ndis802_11Encryption2Enabled;
							else if ((strcmp(tmpbuf, "AES") == 0) || (strcmp(tmpbuf, "aes") == 0))
								pAd->StaCfg.WepStatus	= Ndis802_11Encryption3Enabled;
							else
								pAd->StaCfg.WepStatus	= Ndis802_11WEPDisabled;

							// Update all wepstatus related
							pAd->StaCfg.PairCipher		= pAd->StaCfg.WepStatus;
							pAd->StaCfg.GroupCipher 	= pAd->StaCfg.WepStatus;
							pAd->StaCfg.OrigWepStatus 	= pAd->StaCfg.WepStatus;
							pAd->StaCfg.bMixCipher 		= FALSE;

							//RTMPMakeRSNIE(pAd, pAd->StaCfg.AuthMode, pAd->StaCfg.WepStatus, 0);
							DBGPRINT(RT_DEBUG_TRACE, ("%s::(EncrypType=%d)\n", __func__, pAd->StaCfg.WepStatus));
						}
					}

					IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
					{
						if(RTMPGetCriticalParameter("WPAPSK", tmpbuf, 512, buffer))
						{
							int     err=0;

							tmpbuf[strlen(tmpbuf)] = '\0'; // make STA can process .$^& for WPAPSK input

							if ((pAd->StaCfg.AuthMode != Ndis802_11AuthModeWPAPSK) &&
								(pAd->StaCfg.AuthMode != Ndis802_11AuthModeWPA2PSK) &&
								(pAd->StaCfg.AuthMode != Ndis802_11AuthModeWPANone)
								)
							{
								err = 1;
							}
							else if ((strlen(tmpbuf) >= 8) && (strlen(tmpbuf) < 64))
							{
								PasswordHash((char *)tmpbuf, pAd->CommonCfg.Ssid, pAd->CommonCfg.SsidLen, keyMaterial);
								NdisMoveMemory(pAd->StaCfg.PMK, keyMaterial, 32);

							}
							else if (strlen(tmpbuf) == 64)
							{
								AtoH(tmpbuf, keyMaterial, 32);
								NdisMoveMemory(pAd->StaCfg.PMK, keyMaterial, 32);
							}
							else
							{
								err = 1;
								DBGPRINT(RT_DEBUG_ERROR, ("%s::(WPAPSK key-string required 8 ~ 64 characters!)\n", __func__));
							}

							if (err == 0)
	                        			{
	                        				if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK) ||
									(pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK))
								{
									// Start STA supplicant state machine
									pAd->StaCfg.WpaState = SS_START;
								}
								else if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPANone)
								{
	/*
									NdisZeroMemory(&pAd->SharedKey[BSS0][0], sizeof(CIPHER_KEY));
									pAd->SharedKey[BSS0][0].KeyLen = LEN_TKIP_EK;
									NdisMoveMemory(pAd->SharedKey[BSS0][0].Key, pAd->StaCfg.PMK, LEN_TKIP_EK);
									NdisMoveMemory(pAd->SharedKey[BSS0][0].RxMic, &pAd->StaCfg.PMK[16], LEN_TKIP_RXMICK);
									NdisMoveMemory(pAd->SharedKey[BSS0][0].TxMic, &pAd->StaCfg.PMK[16], LEN_TKIP_TXMICK);

									// Decide its ChiperAlg
									if (pAd->StaCfg.PairCipher == Ndis802_11Encryption2Enabled)
										pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_TKIP;
									else if (pAd->StaCfg.PairCipher == Ndis802_11Encryption3Enabled)
										pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_AES;
									else
										pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_NONE;
	*/
									pAd->StaCfg.WpaState = SS_NOTUSE;
								}

								DBGPRINT(RT_DEBUG_TRACE, ("%s::(WPAPSK=%s)\n", __func__, tmpbuf));
							}
						}
					}

					//DefaultKeyID, KeyType, KeyStr
					rtmp_read_key_parms_from_file(pAd, tmpbuf, buffer);


					//HSCounter
					/*if(RTMPGetKeyParameter("HSCounter", tmpbuf, 10, buffer))
					{
						switch (simple_strtol(tmpbuf, 0, 10))
						{
							case 1: //Enable
								pAd->CommonCfg.bEnableHSCounter = TRUE;
								break;
							case 0: //Disable
							default:
								pAd->CommonCfg.bEnableHSCounter = FALSE;
								break;
						}
						DBGPRINT(RT_DEBUG_TRACE, "HSCounter=%d\n", pAd->CommonCfg.bEnableHSCounter);
					}*/

					HTParametersHook(pAd, tmpbuf, buffer);

					IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
					{
						//PSMode
						if (RTMPGetKeyParameter("PSMode", tmpbuf, 10, buffer))
						{
							if (pAd->StaCfg.BssType == BSS_INFRA)
							{
								if ((strcmp(tmpbuf, "MAX_PSP") == 0) || (strcmp(tmpbuf, "max_psp") == 0))
								{
									// do NOT turn on PSM bit here, wait until MlmeCheckForPsmChange()
									// to exclude certain situations.
									//	   MlmeSetPsm(pAd, PWR_SAVE);
									OPSTATUS_SET_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM);
									if (pAd->StaCfg.bWindowsACCAMEnable == FALSE)
										pAd->StaCfg.WindowsPowerMode = Ndis802_11PowerModeMAX_PSP;
									pAd->StaCfg.WindowsBatteryPowerMode = Ndis802_11PowerModeMAX_PSP;
									pAd->StaCfg.DefaultListenCount = 5;
								}
								else if ((strcmp(tmpbuf, "Fast_PSP") == 0) || (strcmp(tmpbuf, "fast_psp") == 0)
									|| (strcmp(tmpbuf, "FAST_PSP") == 0))
								{
									// do NOT turn on PSM bit here, wait until MlmeCheckForPsmChange()
									// to exclude certain situations.
									//	   MlmeSetPsmBit(pAd, PWR_SAVE);
									OPSTATUS_SET_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM);
									if (pAd->StaCfg.bWindowsACCAMEnable == FALSE)
										pAd->StaCfg.WindowsPowerMode = Ndis802_11PowerModeFast_PSP;
									pAd->StaCfg.WindowsBatteryPowerMode = Ndis802_11PowerModeFast_PSP;
									pAd->StaCfg.DefaultListenCount = 3;
								}
								else if ((strcmp(tmpbuf, "Legacy_PSP") == 0) || (strcmp(tmpbuf, "legacy_psp") == 0)
									|| (strcmp(tmpbuf, "LEGACY_PSP") == 0))
								{
									// do NOT turn on PSM bit here, wait until MlmeCheckForPsmChange()
									// to exclude certain situations.
									//	   MlmeSetPsmBit(pAd, PWR_SAVE);
									OPSTATUS_SET_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM);
									if (pAd->StaCfg.bWindowsACCAMEnable == FALSE)
										pAd->StaCfg.WindowsPowerMode = Ndis802_11PowerModeLegacy_PSP;
									pAd->StaCfg.WindowsBatteryPowerMode = Ndis802_11PowerModeLegacy_PSP;
									pAd->StaCfg.DefaultListenCount = 3;
								}
								else
								{ //Default Ndis802_11PowerModeCAM
									// clear PSM bit immediately
									MlmeSetPsmBit(pAd, PWR_ACTIVE);
									OPSTATUS_SET_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM);
									if (pAd->StaCfg.bWindowsACCAMEnable == FALSE)
										pAd->StaCfg.WindowsPowerMode = Ndis802_11PowerModeCAM;
									pAd->StaCfg.WindowsBatteryPowerMode = Ndis802_11PowerModeCAM;
								}
								DBGPRINT(RT_DEBUG_TRACE, ("PSMode=%ld\n", pAd->StaCfg.WindowsPowerMode));
							}
						}
						// FastRoaming
						if (RTMPGetKeyParameter("FastRoaming", tmpbuf, 32, buffer))
						{
							if (simple_strtol(tmpbuf, 0, 10) == 0)
								pAd->StaCfg.bFastRoaming = FALSE;
							else
								pAd->StaCfg.bFastRoaming = TRUE;

							DBGPRINT(RT_DEBUG_TRACE, ("FastRoaming=%d\n", pAd->StaCfg.bFastRoaming));
						}
						// RoamThreshold
						if (RTMPGetKeyParameter("RoamThreshold", tmpbuf, 32, buffer))
						{
							long lInfo = simple_strtol(tmpbuf, 0, 10);

							if (lInfo > 90 || lInfo < 60)
								pAd->StaCfg.dBmToRoam = -70;
							else
								pAd->StaCfg.dBmToRoam = (CHAR)(-1)*lInfo;

							DBGPRINT(RT_DEBUG_TRACE, ("RoamThreshold=%d  dBm\n", pAd->StaCfg.dBmToRoam));
						}

						if(RTMPGetKeyParameter("TGnWifiTest", tmpbuf, 10, buffer))
						{
							if(simple_strtol(tmpbuf, 0, 10) == 0)
								pAd->StaCfg.bTGnWifiTest = FALSE;
							else
								pAd->StaCfg.bTGnWifiTest = TRUE;
								DBGPRINT(RT_DEBUG_TRACE, ("TGnWifiTest=%d\n", pAd->StaCfg.bTGnWifiTest));
						}
					}
				}
			}
			else
			{
				DBGPRINT(RT_DEBUG_TRACE, ("--> %s does not have a write method\n", src));
			}

			retval=filp_close(srcf,NULL);

			if (retval)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("--> Error %d closing %s\n", -retval, src));
			}
		}
	}

	set_fs(orgfs);
#if 0
	current->fsuid = orgfsuid;
	current->fsgid = orgfsgid;
#endif

	kfree(buffer);
	kfree(tmpbuf);

	return (NDIS_STATUS_SUCCESS);
}

static void	HTParametersHook(
	IN	PRTMP_ADAPTER pAd,
	IN	CHAR		  *pValueStr,
	IN	CHAR		  *pInput)
{

	INT Value;

    if (RTMPGetKeyParameter("HT_PROTECT", pValueStr, 25, pInput))
    {
        Value = simple_strtol(pValueStr, 0, 10);
        if (Value == 0)
        {
            pAd->CommonCfg.bHTProtect = FALSE;
        }
        else
        {
            pAd->CommonCfg.bHTProtect = TRUE;
        }
        DBGPRINT(RT_DEBUG_TRACE, ("HT: Protection  = %s\n", (Value==0) ? "Disable" : "Enable"));
    }

    if (RTMPGetKeyParameter("HT_MIMOPSEnable", pValueStr, 25, pInput))
    {
        Value = simple_strtol(pValueStr, 0, 10);
        if (Value == 0)
        {
            pAd->CommonCfg.bMIMOPSEnable = FALSE;
        }
        else
        {
            pAd->CommonCfg.bMIMOPSEnable = TRUE;
        }
        DBGPRINT(RT_DEBUG_TRACE, ("HT: MIMOPSEnable  = %s\n", (Value==0) ? "Disable" : "Enable"));
    }


    if (RTMPGetKeyParameter("HT_MIMOPSMode", pValueStr, 25, pInput))
    {
        Value = simple_strtol(pValueStr, 0, 10);
        if (Value > MMPS_ENABLE)
        {
			pAd->CommonCfg.BACapability.field.MMPSmode = MMPS_ENABLE;
        }
        else
        {
            //TODO: add mimo power saving mechanism
            pAd->CommonCfg.BACapability.field.MMPSmode = MMPS_ENABLE;
			//pAd->CommonCfg.BACapability.field.MMPSmode = Value;
        }
        DBGPRINT(RT_DEBUG_TRACE, ("HT: MIMOPS Mode  = %d\n", Value));
    }

    if (RTMPGetKeyParameter("HT_BADecline", pValueStr, 25, pInput))
    {
        Value = simple_strtol(pValueStr, 0, 10);
        if (Value == 0)
        {
            pAd->CommonCfg.bBADecline = FALSE;
        }
        else
        {
            pAd->CommonCfg.bBADecline = TRUE;
        }
        DBGPRINT(RT_DEBUG_TRACE, ("HT: BA Decline  = %s\n", (Value==0) ? "Disable" : "Enable"));
    }


    if (RTMPGetKeyParameter("HT_DisableReordering", pValueStr, 25, pInput))
    {
        Value = simple_strtol(pValueStr, 0, 10);
        if (Value == 0)
        {
            pAd->CommonCfg.bDisableReordering = FALSE;
        }
        else
        {
            pAd->CommonCfg.bDisableReordering = TRUE;
        }
        DBGPRINT(RT_DEBUG_TRACE, ("HT: DisableReordering  = %s\n", (Value==0) ? "Disable" : "Enable"));
    }

    if (RTMPGetKeyParameter("HT_AutoBA", pValueStr, 25, pInput))
    {
        Value = simple_strtol(pValueStr, 0, 10);
        if (Value == 0)
        {
            pAd->CommonCfg.BACapability.field.AutoBA = FALSE;
        }
        else
        {
            pAd->CommonCfg.BACapability.field.AutoBA = TRUE;
        }
        pAd->CommonCfg.REGBACapability.field.AutoBA = pAd->CommonCfg.BACapability.field.AutoBA;
        DBGPRINT(RT_DEBUG_TRACE, ("HT: Auto BA  = %s\n", (Value==0) ? "Disable" : "Enable"));
    }

	// Tx_+HTC frame
    if (RTMPGetKeyParameter("HT_HTC", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);
		if (Value == 0)
		{
			pAd->HTCEnable = FALSE;
		}
		else
		{
            		pAd->HTCEnable = TRUE;
		}
		DBGPRINT(RT_DEBUG_TRACE, ("HT: Tx +HTC frame = %s\n", (Value==0) ? "Disable" : "Enable"));
	}

	// Enable HT Link Adaptation Control
	if (RTMPGetKeyParameter("HT_LinkAdapt", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);
		if (Value == 0)
		{
			pAd->bLinkAdapt = FALSE;
		}
		else
		{
			pAd->HTCEnable = TRUE;
			pAd->bLinkAdapt = TRUE;
		}
		DBGPRINT(RT_DEBUG_TRACE, ("HT: Link Adaptation Control = %s\n", (Value==0) ? "Disable" : "Enable(+HTC)"));
	}

	// Reverse Direction Mechanism
    if (RTMPGetKeyParameter("HT_RDG", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);
		if (Value == 0)
		{
			pAd->CommonCfg.bRdg = FALSE;
		}
		else
		{
			pAd->HTCEnable = TRUE;
            pAd->CommonCfg.bRdg = TRUE;
		}
		DBGPRINT(RT_DEBUG_TRACE, ("HT: RDG = %s\n", (Value==0) ? "Disable" : "Enable(+HTC)"));
	}




	// Tx A-MSUD ?
    if (RTMPGetKeyParameter("HT_AMSDU", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);
		if (Value == 0)
		{
			pAd->CommonCfg.BACapability.field.AmsduEnable = FALSE;
		}
		else
		{
            pAd->CommonCfg.BACapability.field.AmsduEnable = TRUE;
		}
		DBGPRINT(RT_DEBUG_TRACE, ("HT: Tx A-MSDU = %s\n", (Value==0) ? "Disable" : "Enable"));
	}

	// MPDU Density
    if (RTMPGetKeyParameter("HT_MpduDensity", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);
		if (Value <=7 && Value >= 0)
		{
			pAd->CommonCfg.BACapability.field.MpduDensity = Value;
			DBGPRINT(RT_DEBUG_TRACE, ("HT: MPDU Density = %d\n", Value));
		}
		else
		{
			pAd->CommonCfg.BACapability.field.MpduDensity = 4;
			DBGPRINT(RT_DEBUG_TRACE, ("HT: MPDU Density = %d (Default)\n", 4));
		}
	}

	// Max Rx BA Window Size
    if (RTMPGetKeyParameter("HT_BAWinSize", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);

		if (Value >=1 && Value <= 64)
		{
			pAd->CommonCfg.REGBACapability.field.RxBAWinLimit = Value;
			pAd->CommonCfg.BACapability.field.RxBAWinLimit = Value;
			DBGPRINT(RT_DEBUG_TRACE, ("HT: BA Windw Size = %d\n", Value));
		}
		else
		{
            pAd->CommonCfg.REGBACapability.field.RxBAWinLimit = 64;
			pAd->CommonCfg.BACapability.field.RxBAWinLimit = 64;
			DBGPRINT(RT_DEBUG_TRACE, ("HT: BA Windw Size = 64 (Defualt)\n"));
		}

	}

	// Guard Interval
	if (RTMPGetKeyParameter("HT_GI", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);

		if (Value == GI_400)
		{
			pAd->CommonCfg.RegTransmitSetting.field.ShortGI = GI_400;
		}
		else
		{
			pAd->CommonCfg.RegTransmitSetting.field.ShortGI = GI_800;
		}

		DBGPRINT(RT_DEBUG_TRACE, ("HT: Guard Interval = %s\n", (Value==GI_400) ? "400" : "800" ));
	}

	// HT Operation Mode : Mixed Mode , Green Field
	if (RTMPGetKeyParameter("HT_OpMode", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);

		if (Value == HTMODE_GF)
		{

			pAd->CommonCfg.RegTransmitSetting.field.HTMODE  = HTMODE_GF;
		}
		else
		{
			pAd->CommonCfg.RegTransmitSetting.field.HTMODE  = HTMODE_MM;
		}

		DBGPRINT(RT_DEBUG_TRACE, ("HT: Operate Mode = %s\n", (Value==HTMODE_GF) ? "Green Field" : "Mixed Mode" ));
	}

	// Fixed Tx mode : CCK, OFDM
	if (RTMPGetKeyParameter("FixedTxMode", pValueStr, 25, pInput))
	{
		UCHAR	fix_tx_mode;

		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			fix_tx_mode = FIXED_TXMODE_HT;

			if (strcmp(pValueStr, "OFDM") == 0 || strcmp(pValueStr, "ofdm") == 0)
			{
				fix_tx_mode = FIXED_TXMODE_OFDM;
			}
			else if (strcmp(pValueStr, "CCK") == 0 || strcmp(pValueStr, "cck") == 0)
			{
		        fix_tx_mode = FIXED_TXMODE_CCK;
			}
			else if (strcmp(pValueStr, "HT") == 0 || strcmp(pValueStr, "ht") == 0)
			{
		        fix_tx_mode = FIXED_TXMODE_HT;
		}
		else
		{
				Value = simple_strtol(pValueStr, 0, 10);
				// 1 : CCK
				// 2 : OFDM
				// otherwise : HT
				if (Value == FIXED_TXMODE_CCK || Value == FIXED_TXMODE_OFDM)
					fix_tx_mode = Value;
				else
					fix_tx_mode = FIXED_TXMODE_HT;
		}

			pAd->StaCfg.DesiredTransmitSetting.field.FixedTxMode = fix_tx_mode;
			DBGPRINT(RT_DEBUG_TRACE, ("Fixed Tx Mode = %d\n", fix_tx_mode));

		}
	}


	// Channel Width
	if (RTMPGetKeyParameter("HT_BW", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);

		if (Value == BW_40)
		{
			pAd->CommonCfg.RegTransmitSetting.field.BW  = BW_40;
		}
		else
		{
            pAd->CommonCfg.RegTransmitSetting.field.BW  = BW_20;
		}

#ifdef MCAST_RATE_SPECIFIC
		pAd->CommonCfg.MCastPhyMode.field.BW = pAd->CommonCfg.RegTransmitSetting.field.BW;
#endif // MCAST_RATE_SPECIFIC //

		DBGPRINT(RT_DEBUG_TRACE, ("HT: Channel Width = %s\n", (Value==BW_40) ? "40 MHz" : "20 MHz" ));
	}

	if (RTMPGetKeyParameter("HT_EXTCHA", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);

		if (Value == 0)
		{

			pAd->CommonCfg.RegTransmitSetting.field.EXTCHA  = EXTCHA_BELOW;
		}
		else
		{
            pAd->CommonCfg.RegTransmitSetting.field.EXTCHA = EXTCHA_ABOVE;
		}

		DBGPRINT(RT_DEBUG_TRACE, ("HT: Ext Channel = %s\n", (Value==0) ? "BELOW" : "ABOVE" ));
	}

	// MSC
	if (RTMPGetKeyParameter("HT_MCS", pValueStr, 50, pInput))
	{
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			Value = simple_strtol(pValueStr, 0, 10);

//			if ((Value >= 0 && Value <= 15) || (Value == 32))
			if ((Value >= 0 && Value <= 23) || (Value == 32)) // 3*3
		{
				pAd->StaCfg.DesiredTransmitSetting.field.MCS  = Value;
				pAd->StaCfg.bAutoTxRateSwitch = FALSE;
				DBGPRINT(RT_DEBUG_TRACE, ("HT: MCS = %d\n", pAd->StaCfg.DesiredTransmitSetting.field.MCS));
		}
		else
		{
				pAd->StaCfg.DesiredTransmitSetting.field.MCS  = MCS_AUTO;
				pAd->StaCfg.bAutoTxRateSwitch = TRUE;
				DBGPRINT(RT_DEBUG_TRACE, ("HT: MCS = AUTO\n"));
		}
	}
	}

	// STBC
    if (RTMPGetKeyParameter("HT_STBC", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);
		if (Value == STBC_USE)
		{
			pAd->CommonCfg.RegTransmitSetting.field.STBC = STBC_USE;
		}
		else
		{
			pAd->CommonCfg.RegTransmitSetting.field.STBC = STBC_NONE;
		}
		DBGPRINT(RT_DEBUG_TRACE, ("HT: STBC = %d\n", pAd->CommonCfg.RegTransmitSetting.field.STBC));
	}

	// 40_Mhz_Intolerant
	if (RTMPGetKeyParameter("HT_40MHZ_INTOLERANT", pValueStr, 25, pInput))
	{
		Value = simple_strtol(pValueStr, 0, 10);
		if (Value == 0)
		{
			pAd->CommonCfg.bForty_Mhz_Intolerant = FALSE;
		}
		else
		{
			pAd->CommonCfg.bForty_Mhz_Intolerant = TRUE;
		}
		DBGPRINT(RT_DEBUG_TRACE, ("HT: 40MHZ INTOLERANT = %d\n", pAd->CommonCfg.bForty_Mhz_Intolerant));
	}
	//HT_TxStream
	if(RTMPGetKeyParameter("HT_TxStream", pValueStr, 10, pInput))
	{
		switch (simple_strtol(pValueStr, 0, 10))
		{
			case 1:
				pAd->CommonCfg.TxStream = 1;
				break;
			case 2:
				pAd->CommonCfg.TxStream = 2;
				break;
			case 3: // 3*3
			default:
				pAd->CommonCfg.TxStream = 3;

				if (pAd->MACVersion < RALINK_2883_VERSION)
					pAd->CommonCfg.TxStream = 2; // only 2 tx streams for RT2860 series
				break;
		}
		DBGPRINT(RT_DEBUG_TRACE, ("HT: Tx Stream = %d\n", pAd->CommonCfg.TxStream));
	}
	//HT_RxStream
	if(RTMPGetKeyParameter("HT_RxStream", pValueStr, 10, pInput))
	{
		switch (simple_strtol(pValueStr, 0, 10))
		{
			case 1:
				pAd->CommonCfg.RxStream = 1;
				break;
			case 2:
				pAd->CommonCfg.RxStream = 2;
				break;
			case 3:
			default:
				pAd->CommonCfg.RxStream = 3;

				if (pAd->MACVersion < RALINK_2883_VERSION)
					pAd->CommonCfg.RxStream = 2; // only 2 rx streams for RT2860 series
				break;
		}
		DBGPRINT(RT_DEBUG_TRACE, ("HT: Rx Stream = %d\n", pAd->CommonCfg.RxStream));
	}

}
