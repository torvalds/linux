#!/usr/bin/awk
#
# Version history:
#  v4+ Adapted for OpenSSH Portable (see cvs Id and history)
#  v3, I put the program under a proper license
#      Dan Nelson <dnelson@allantgroup.com> added .An, .Aq and fixed a typo
#  v2, fixed to work on GNU awk --posix and MacOS X
#  v1, first attempt, didn't work on MacOS X
#
# Copyright (c) 2003 Peter Stuge <stuge-mdoc2man@cdy.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


BEGIN {
  optlist=0
  oldoptlist=0
  nospace=0
  synopsis=0
  reference=0
  block=0
  ext=0
  extopt=0
  literal=0
  prenl=0
  breakw=0
  line=""
}

function wtail() {
  retval=""
  while(w<nwords) {
    if(length(retval))
      retval=retval OFS
    retval=retval words[++w]
  }
  return retval
}

function add(str) {
  for(;prenl;prenl--)
    line=line "\n"
  line=line str
}

! /^\./ {
  for(;prenl;prenl--)
    print ""
  print
  if(literal)
    print ".br"
  next
}

/^\.\\"/ { next }

{
  option=0
  parens=0
  angles=0
  sub("^\\.","")
  nwords=split($0,words)
  for(w=1;w<=nwords;w++) {
    skip=0
    if(match(words[w],"^Li|Pf$")) {
      skip=1
    } else if(match(words[w],"^Xo$")) {
      skip=1
      ext=1
      if(length(line)&&!(match(line," $")||prenl))
	add(OFS)
    } else if(match(words[w],"^Xc$")) {
      skip=1
      ext=0
      if(!extopt)
	prenl++
      w=nwords
    } else if(match(words[w],"^Bd$")) {
      skip=1
      if(match(words[w+1],"-literal")) {
	literal=1
	prenl++
	w=nwords
      }
    } else if(match(words[w],"^Ed$")) {
      skip=1
      literal=0
    } else if(match(words[w],"^Ns$")) {
      skip=1
      if(!nospace)
	nospace=1
      sub(" $","",line)
    } else if(match(words[w],"^No$")) {
      skip=1
      sub(" $","",line)
      add(words[++w])
    } else if(match(words[w],"^Dq$")) {
      skip=1
      add("``")
      add(words[++w])
      while(w<nwords&&!match(words[w+1],"^[\\.,]"))
	add(OFS words[++w])
      add("''")
      if(!nospace&&match(words[w+1],"^[\\.,]"))
	nospace=1
    } else if(match(words[w],"^Sq|Ql$")) {
      skip=1
      add("`" words[++w] "'")
      if(!nospace&&match(words[w+1],"^[\\.,]"))
	nospace=1
    } else if(match(words[w],"^Oo$")) {
      skip=1
      extopt=1
      if(!nospace)
	nospace=1
      add("[")
    } else if(match(words[w],"^Oc$")) {
      skip=1
      extopt=0
      add("]")
    }
    if(!skip) {
      if(!nospace&&length(line)&&!(match(line," $")||prenl))
	add(OFS)
      if(nospace==1)
	nospace=0
    }
    if(match(words[w],"^Dd$")) {
      if(match(words[w+1],"^\\$Mdocdate:")) {
        w++;
        if(match(words[w+4],"^\\$$")) {
          words[w+4] = ""
        }
      }
      date=wtail()
      next
    } else if(match(words[w],"^Dt$")) {
      id=wtail()
      next
    } else if(match(words[w],"^Ux$")) {
      add("UNIX")
      skip=1
    } else if(match(words[w],"^Ox$")) {
      add("OpenBSD")
      skip=1
    } else if(match(words[w],"^Os$")) {
      add(".TH " id " \"" date "\" \"" wtail() "\"")
    } else if(match(words[w],"^Sh$")) {
      add(".SH")
      synopsis=match(words[w+1],"SYNOPSIS")
    } else if(match(words[w],"^Xr$")) {
      add("\\fB" words[++w] "\\fP(" words[++w] ")" words[++w])
    } else if(match(words[w],"^Rs$")) {
      split("",refauthors)
      nrefauthors=0
      reftitle=""
      refissue=""
      refdate=""
      refopt=""
      refreport=""
      reference=1
      next
    } else if(match(words[w],"^Re$")) {
      prenl++
      for(i=nrefauthors-1;i>0;i--) {
	add(refauthors[i])
	if(i>1)
	  add(", ")
      }
      if(nrefauthors>1)
	add(" and ")
      if(nrefauthors>0)
        add(refauthors[0] ", ")
      add("\\fI" reftitle "\\fP")
      if(length(refissue))
	add(", " refissue)
      if(length(refreport)) {
	add(", " refreport)
      }
      if(length(refdate))
	add(", " refdate)
      if(length(refopt))
	add(", " refopt)
      add(".")
      reference=0
    } else if(reference) {
      if(match(words[w],"^%A$")) { refauthors[nrefauthors++]=wtail() }
      if(match(words[w],"^%T$")) {
	reftitle=wtail()
	sub("^\"","",reftitle)
	sub("\"$","",reftitle)
      }
      if(match(words[w],"^%N$")) { refissue=wtail() }
      if(match(words[w],"^%D$")) { refdate=wtail() }
      if(match(words[w],"^%O$")) { refopt=wtail() }
      if(match(words[w],"^%R$")) { refreport=wtail() }
    } else if(match(words[w],"^Nm$")) {
      if(synopsis) {
	add(".br")
	prenl++
      }
      n=words[++w]
      if(!length(name))
	name=n
      if(!length(n))
	n=name
      add("\\fB" n "\\fP")
      if(!nospace&&match(words[w+1],"^[\\.,]"))
	nospace=1
    } else if(match(words[w],"^Nd$")) {
      add("\\- " wtail())
    } else if(match(words[w],"^Fl$")) {
      add("\\fB\\-" words[++w] "\\fP")
      if(!nospace&&match(words[w+1],"^[\\.,]"))
	nospace=1
    } else if(match(words[w],"^Ar$")) {
      add("\\fI")
      if(w==nwords)
	add("file ...\\fP")
      else {
	add(words[++w] "\\fP")
	while(match(words[w+1],"^\\|$"))
	  add(OFS words[++w] " \\fI" words[++w] "\\fP")
      }
      if(!nospace&&match(words[w+1],"^[\\.,]"))
	nospace=1
    } else if(match(words[w],"^Cm$")) {
      add("\\fB" words[++w] "\\fP")
      while(w<nwords&&match(words[w+1],"^[\\.,:;)]"))
	add(words[++w])
    } else if(match(words[w],"^Op$")) {
      option=1
      if(!nospace)
	nospace=1
      add("[")
    } else if(match(words[w],"^Pp$")) {
      prenl++
    } else if(match(words[w],"^An$")) {
      prenl++
    } else if(match(words[w],"^Ss$")) {
      add(".SS")
    } else if(match(words[w],"^Pa$")&&!option) {
      add("\\fI")
      w++
      if(match(words[w],"^\\."))
	add("\\&")
      add(words[w] "\\fP")
      while(w<nwords&&match(words[w+1],"^[\\.,:;)]"))
	add(words[++w])
    } else if(match(words[w],"^Dv$")) {
      add(".BR")
    } else if(match(words[w],"^Em|Ev$")) {
      add(".IR")
    } else if(match(words[w],"^Pq$")) {
      add("(")
      nospace=1
      parens=1
    } else if(match(words[w],"^Aq$")) {
      add("<")
      nospace=1
      angles=1
    } else if(match(words[w],"^S[xy]$")) {
      add(".B " wtail())
    } else if(match(words[w],"^Ic$")) {
      plain=1
      add("\\fB")
      while(w<nwords) {
	w++
	if(match(words[w],"^Op$")) {
	  w++
	  add("[")
	  words[nwords]=words[nwords] "]"
	}
	if(match(words[w],"^Ar$")) {
	  add("\\fI" words[++w] "\\fP")
	} else if(match(words[w],"^[\\.,]")) {
	  sub(" $","",line)
	  if(plain) {
	    add("\\fP")
	    plain=0
	  }
	  add(words[w])
	} else {
	  if(!plain) {
	    add("\\fB")
	    plain=1
	  }
	  add(words[w])
	}
	if(!nospace)
	  add(OFS)
      }
      sub(" $","",line)
      if(plain)
	add("\\fP")
    } else if(match(words[w],"^Bl$")) {
      oldoptlist=optlist
      if(match(words[w+1],"-bullet"))
	optlist=1
      else if(match(words[w+1],"-enum")) {
	optlist=2
	enum=0
      } else if(match(words[w+1],"-tag"))
	optlist=3
      else if(match(words[w+1],"-item"))
	optlist=4
      else if(match(words[w+1],"-bullet"))
	optlist=1
      w=nwords
    } else if(match(words[w],"^El$")) {
      optlist=oldoptlist
      if(!optlist)
        add(".PP")
    } else if(match(words[w],"^Bk$")) {
      if(match(words[w+1],"-words")) {
	w++
	breakw=1
      }
    } else if(match(words[w],"^Ek$")) {
      breakw=0
    } else if(match(words[w],"^It$")&&optlist) {
      if(optlist==1)
	add(".IP \\(bu")
      else if(optlist==2)
	add(".IP " ++enum ".")
      else if(optlist==3) {
	add(".TP")
	prenl++
	if(match(words[w+1],"^Pa$|^Ev$")) {
	  add(".B")
	  w++
	}
      } else if(optlist==4)
	add(".IP")
    } else if(match(words[w],"^Sm$")) {
      if(match(words[w+1],"off"))
	nospace=2
      else if(match(words[w+1],"on"))
	nospace=0
      w++
    } else if(!skip) {
      add(words[w])
    }
  }
  if(match(line,"^\\.[^a-zA-Z]"))
    sub("^\\.","",line)
  if(parens)
    add(")")
  if(angles)
    add(">")
  if(option)
    add("]")
  if(ext&&!extopt&&!match(line," $"))
    add(OFS)
  if(!ext&&!extopt&&length(line)) {
    print line
    prenl=0
    line=""
  }
}
