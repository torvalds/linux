#
# $tcsh: complete.tcsh,v 1.56 2015/07/03 16:52:47 christos Exp $
# example file using the new completion code
#
# Debian GNU/Linux
# /usr/share/doc/tcsh/examples/complete.gz
#
# This file may be read from user's ~/.cshrc or ~/.tcshrc file by
# decompressing it into the home directory as ~/.complete and
# then adding the line "source ~/.complete" and maybe defining
# some of the shell variables described below.
#
# Added two Debian-specific completions: dpkg and dpkg-deb (who
# wrote them?). Changed completions of several commands. The ones
# are evaluated if the `traditional_complete' shell variable is
# defined.
#
# Debian enhancements by Vadim Vygonets <vadik@cs.huji.ac.il>.
# Bugfixes and apt completions by Miklos Quartus <miklos.quartus@nokia.com>.
# Cleanup by Martin A. Godisch <martin@godisch.de>.

onintr -
if ( ! $?prompt ) goto end

if ( $?tcsh ) then
  if ( $tcsh != 1 ) then
    set rev=$tcsh:r:r
    set rel=$tcsh:r:e
    if ( $rev > 6 || ( $rev > 5 && $rel > 1 ) ) then
      set _has_complete=1
    endif
  endif
  unset rev rel
endif

if ( ! $?_has_complete ) goto end

if ( ! $?noglob ) set noglob _unset_noglob

# Old TCSH versions don't define OSTYPE.
# Use a close approximation instead.

if ( ! $?OSTYPE ) then
  setenv OSTYPE `echo "$HOSTTYPE" | sed -e 's/^(i[3456]86|(amd|x86_)64)-//'`
endif

if ( ! $?hosts ) set hosts

foreach f ( "$HOME/."{,r,ssh/known_}hosts* \
  /usr/local/etc/csh.hosts /etc/hosts.equiv )
  if ( -r "$f" ) then
    set hosts=($hosts `sed \
      -e 's/#.*//' \
      -e '/^[+-]@/d' \
      -e 's/^[-+]//' \
      -e 's/[[:space:]].*//' \
      -e 's/,/\n/g' "$f" \
      | sed -e '/^[.:[:xdigit:][:space:]]*$/d'`)
  endif
end
unset f

if ( -r "$HOME/.netrc" ) then
  set hosts=($hosts `awk '$1 == "machine" { print $2 }' "$HOME/.netrc"`)
endif

set hosts=(`echo $hosts | tr ' ' '\012' | sort -u`)

if ( ! $#hosts ) then
  # This is just a hint for the user.
  set hosts=(ftp.funet.fi ftp.gnu.org ftp.uu.net)
endif

complete ywho		n/*/\$hosts/	# argument from list in $hosts
complete rsh		p/1/\$hosts/ c/-/"(l n)"/   n/-l/u/ N/-l/c/ n/-/c/ p/2/c/ p/*/f/
complete ssh		p/1/\$hosts/ c/-/"(l n)"/   n/-l/u/ N/-l/c/ n/-/c/ p/2/c/ p/*/f/
complete xrsh		p/1/\$hosts/ c/-/"(l 8 e)"/ n/-l/u/ N/-l/c/ n/-/c/ p/2/c/ p/*/f/
complete rlogin 	p/1/\$hosts/ c/-/"(l 8 e)"/ n/-l/u/
complete telnet 	p/1/\$hosts/ p/2/x:'<port>'/ n/*/n/

complete cd  		p/1/d/		# Directories only
complete chdir 		p/1/d/
complete pushd 		p/1/d/
complete popd 		p/1/d/
complete pu 		p/1/d/
complete po 		p/1/d/
complete complete 	p/1/X/		# Completions only
complete uncomplete	n/*/X/
complete exec 		p/1/c/		# Commands only
complete trace 		p/1/c/
complete strace 	p/1/c/
complete which		n/*/c/
complete where		n/*/c/
complete skill 		p/1/c/
complete dde		p/1/c/ 
complete adb		c/-I/d/ n/-/c/ N/-/"(core)"/ p/1/c/ p/2/"(core)"/
complete sdb		p/1/c/
complete dbx		c/-I/d/ n/-/c/ N/-/"(core)"/ p/1/c/ p/2/"(core)"/
complete xdb		p/1/c/
complete gdb		n/-d/d/ n/*/c/
complete ups		p/1/c/
complete set		'c/*=/f/' 'p/1/s/=' 'n/=/f/'
complete unset		n/*/s/
complete alias 		p/1/a/		# only aliases are valid
complete unalias	n/*/a/
complete xdvi 		n/*/f:*.dvi/
complete dvips 		n/*/f:*.dvi/
complete tex	 	n/*/f:*.{tex,texi}/
complete latex	 	n/*/f:*.{tex,ltx}/

complete su \
  c/--/"(login fast preserve-environment command shell help version)"/ \
  c/-/"(f l m p c s -)"/ \
  n/{-c,--command}/c/ \
  n@{-s,--shell}@'`cat /etc/shells`'@ \
  n/*/u/
complete cc \
  c/-[IL]/d/ \
  c@-l@'`\ls -1 /usr/lib/lib*.a | sed s%^.\*/lib%%\;s%\\.a\$%%`'@ \
  c/-/"(o l c g L I D U)"/ n/*/f:*.[coasi]/
complete acc \
  c/-[IL]/d/ \
  c@-l@'`\ls -1 /usr/lang/SC1.0/lib*.a | sed s%^.\*/lib%%\;s%\\.a\$%%`'@ \
  c/-/"(o l c g L I D U)"/ n/*/f:*.[coasi]/
complete gcc \
  c/-[IL]/d/ \
  c/-f/"(caller-saves cse-follow-jumps delayed-branch elide-constructors \
	expensive-optimizations float-store force-addr force-mem inline \
	inline-functions keep-inline-functions memoize-lookups \
	no-default-inline no-defer-pop no-function-cse omit-frame-pointer \
	rerun-cse-after-loop schedule-insns schedule-insns2 strength-reduce \
	thread-jumps unroll-all-loops unroll-loops syntax-only all-virtual \
	cond-mismatch dollars-in-identifiers enum-int-equiv no-asm no-builtin \
	no-strict-prototype signed-bitfields signed-char this-is-variable \
	unsigned-bitfields unsigned-char writable-strings call-saved-reg \
	call-used-reg fixed-reg no-common no-gnu-binutils nonnull-objects \
	pcc-struct-return pic PIC shared-data short-enums short-double \
	volatile)"/ \
  c/-W/"(all aggregate-return cast-align cast-qual comment conversion \
	enum-clash error format id-clash-len implicit missing-prototypes \
	no-parentheses pointer-arith return-type shadow strict-prototypes \
	switch uninitialized unused write-strings)"/ \
  c/-m/"(68000 68020 68881 bitfield fpa nobitfield rtd short c68000 c68020 \
	soft-float g gnu unix fpu no-epilogue)"/ \
  c/-d/"(D M N)"/ \
  c/-/"(f W vspec v vpath ansi traditional traditional-cpp trigraphs pedantic \
	x o l c g L I D U O O2 C E H B b V M MD MM i dynamic nodtdlib static \
	nostdinc undef)"/ \
  c/-l/f:*.a/ \
  n/*/f:*.{c,C,cc,o,a,s,i}/
complete g++ 	n/*/f:*.{C,cc,o,s,i}/
complete CC 	n/*/f:*.{C,cc,cpp,o,s,i}/
complete rm \
  c/--/"(directory force interactive verbose recursive help version)"/ \
  c/-/"(d f i v r R -)"/ \
  n/*/f:^*.{c,cc,C,h,in}/
  # Protect precious files
complete vi 	n/*/f:^*.[oa]/
complete bindkey \
  N/-a/b/ N/-c/c/ n/-[ascr]/'x:<key-sequence>'/ \
  n/-[svedlr]/n/ c/-[vedl]/n/ c/-/"(a s k c v e d l r)"/ \
  n/-k/"(left right up down)"/ p/2-/b/ \
  p/1/'x:<key-sequence or option>'/

complete find \
  n/-fstype/"(nfs 4.2)"/ \
  n/-name/f/ \
  n/-type/"(c b d f p l s)"/ \
  n/-user/u/ \
  n/-group/g/ \
  n/-exec/c/ \
  n/-ok/c/ \
  n/-cpio/f/ \
  n/-ncpio/f/ \
  n/-newer/f/ \
  c/-/"(fstype name perm prune type user nouser group nogroup size inum \
	atime mtime ctime exec ok print ls cpio ncpio newer xdev depth \
	daystart follow maxdepth mindepth noleaf version anewer cnewer \
	amin cmin mmin true false uid gid ilname iname ipath iregex links \
	lname empty path regex used xtype fprint fprint0 fprintf print0 \
	printf not a and o or)"/ \
  n/*/d/

complete -%*		c/%/j/			# fill in the jobs builtin
complete {fg,bg,stop}	c/%/j/ p/1/"(%)"//

complete limit		c/-/"(h)"/ n/*/l/
complete unlimit	c/-/"(h)"/ n/*/l/

#complete -co*	p/0/"(compress)"/	# make compress completion
#					    # not ambiguous

# "zcat" may be linked to "compress" or "gzip"
if (-X zcat) then
  zcat --version >& /dev/null
  if ($status != 0) then
    complete zcat	n/*/f:*.Z/
  else
    complete zcat	c/--/"(force help license quiet version)"/ \
			c/-/"(f h L q V -)"/ \
			n/*/f:*.{gz,Z,z,zip}/
  endif
endif

complete finger	c/*@/\$hosts/ n/*/u/@ 
complete ping	p/1/\$hosts/
complete traceroute	p/1/\$hosts/

complete {talk,ntalk,phone} \
  p/1/'`users | tr " " "\012" | uniq`'/ \
  n/*/\`who\ \|\ grep\ \$:1\ \|\ awk\ \'\{\ print\ \$2\ \}\'\`/

complete ftp	c/-/"(d i g n v)"/ n/-/\$hosts/ p/1/\$hosts/ n/*/n/

# this one is simple...
#complete rcp c/*:/f/ C@[./\$~]*@f@ n/*/\$hosts/:
# From Michael Schroeder <mlschroe@immd4.informatik.uni-erlangen.de> 
# This one will rsh to the file to fetch the list of files!
complete rcp 'c%*@*:%`set q=$:-0;set q="$q:s/@/ /";set q="$q:s/:/ /";set q=($q " ");rsh $q[2] -l $q[1] ls -dp $q[3]\*`%' 'c%*:%`set q=$:-0;set q="$q:s/:/ /";set q=($q " ");rsh $q[1] ls -dp $q[2]\*`%' 'c%*@%$hosts%:' 'C@[./$~]*@f@'  'n/*/$hosts/:'

complete dd \
  c/--/"(help version)"/ c/[io]f=/f/ \
  c/conv=*,/"(ascii ebcdic ibm block unblock \
	      lcase notrunc ucase swab noerror sync)"/,\
  c/conv=/"(ascii ebcdic ibm block unblock \
	    lcase notrunc ucase swab noerror sync)"/,\
  c/*=/x:'<number>'/ \
  n/*/"(if of conv ibs obs bs cbs files skip file seek count)"/=

complete nslookup   p/1/x:'<host>'/ p/2/\$hosts/

complete ar \
  c/[dmpqrtx]/"(c l o u v a b i)"/ \
  p/1/"(d m p q r t x)"// \
  p/2/f:*.a/ \
  p/*/f:*.o/

# these should be merged with the MH completion hacks below - jgotts
complete {refile,sprev,snext,scan,pick,rmm,inc,folder,show} \
	    "c@+@F:$HOME/Mail/@"

# these and interrupt handling from Jaap Vermeulen <jaap@sequent.com>
complete {rexec,rxexec,rxterm,rmterm} \
  'p/1/$hosts/' \
  'c/-/(l L E)/' \
  'n/-l/u/' \
  'n/-L/f/' \
  'n/-E/e/' \
  'n/*/c/'
complete kill \
  'c/-/S/' \
  'c/%/j/' \
  'n/*/`ps -u $LOGNAME | awk '"'"'{print $1}'"'"'`/'

# these from Marc Horowitz <marc@cam.ov.com>
complete attach \
  'n/-mountpoint/d/' \
  'n/-m/d/' \
  'n/-type/(afs nfs rvd ufs)/' \
  'n/-t/(afs nfs rvd ufs)/' \
  'n/-user/u/' \
  'n/-U/u/' \
  'c/-/(verbose quiet force printpath lookup debug map nomap remap zephyr \
	nozephyr readonly write mountpoint noexplicit explicit type \
	mountoptions nosetuid setuid override skipfsck lock user host)/' \
  'n/-e/f/' \
  'n/*/()/'
complete hesinfo \
  'p/1/u/' \
  'p/2/(passwd group uid grplist pcap pobox cluster filsys sloc service)/'

complete ./configure \
  'c@--{prefix,exec-prefix,bindir,sbindir,libexecdir,datadir,sysconfdir,sharedstatedir,localstatedir,infodir,mandir,srcdir,x-includes,x-libraries}=*@x:<directory e.g. /usr/local>'@ \
  'c/--cachefile=*/x:<filename>/' \
  'c/--{enable,disable,with}-*/x:<feature>//' \
  'c/--*=/x:<directory>//' \
  'c/--/(prefix= exec-prefix= bindir= sbindir= libexecdir= datadir= \
	sysconfdir= sharedstatedir= localstatedir= infodir= mandir= \
	srcdir= x-includes= x-libraries= cachefile= enable- disable- \
	with- help no-create quiet silent version verbose )//'

complete gs \
  'c/-sDEVICE=/(x11 cdjmono cdj550 epson eps9high epsonc dfaxhigh dfaxlow \
		laserjet ljet4 sparc pbm pbmraw pgm pgmraw ppm ppmraw bit)/' \
  'c/-sOutputFile=/f/' 'c/-s/(DEVICE OutputFile)/=' \
  'c/-d/(NODISPLAY NOPLATFONTS NOPAUSE)/' 'n/*/f/'
complete perl		'n/-S/c/'
complete sccs \
  p/1/"(admin cdc check clean comb deledit delget delta diffs edit enter \
	fix get help info print prs prt rmdel sccsdiff tell unedit unget \
	val what)"/

complete printenv	'n/*/e/'
complete setenv		'p/1/e/' 'c/*:/f/'

# these and method of setting hosts from Kimmo Suominen <kim@tac.nyc.ny.us>
if ( -f "$HOME/.mh_profile" && -X folders ) then 
  if ( ! $?FOLDERS ) setenv FOLDERS "`folders -fast -recurse`"
  if ( ! $?MHA )     setenv MHA     "`ali | sed -e '/^ /d' -e 's/:.*//'`"

  set folders = ( $FOLDERS )
  set mha = ( $MHA )

  complete ali \
    'c/-/(alias nolist list nonormalize normalize nouser user help)/' \
    'n,-alias,f,'

  complete anno \
    'c/-/(component noinplace inplace nodate date text help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete burst \
    'c/-/(noinplace inplace noquiet quiet noverbose verbose help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete comp \
    'c/-/(draftfolder draftmessage nodraftfolder editor noedit file form nouse use whatnowproc nowhatnowproc help)/' \
    'c,+,$folders,'  \
    'n,-whatnowproc,c,'  \
    'n,-file,f,'\
    'n,-form,f,'\
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete dist \
    'c/-/(noannotate annotate draftfolder draftmessage nodraftfolder editor noedit form noinplace inplace whatnowproc nowhatnowproc help)/' \
    'c,+,$folders,'  \
    'n,-whatnowproc,c,'  \
    'n,-form,f,'\
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete folder \
    'c/-/(all nofast fast noheader header nopack pack noverbose verbose norecurse recurse nototal total noprint print nolist list push pop help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete folders \
    'c/-/(all nofast fast noheader header nopack pack noverbose verbose norecurse recurse nototal total noprint print nolist list push pop help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete forw \
    'c/-/(noannotate annotate draftfolder draftmessage nodraftfolder editor noedit filter form noformat format noinplace inplace digest issue volume whatnowproc nowhatnowproc help)/' \
    'c,+,$folders,'  \
    'n,-whatnowproc,c,'  \
    'n,-filter,f,'\
    'n,-form,f,'\
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete inc \
    'c/-/(audit file noaudit nochangecur changecur file form format nosilent silent notruncate truncate width help)/' \
    'c,+,$folders,'  \
    'n,-audit,f,'\
    'n,-form,f,'

  complete mark \
    'c/-/(add delete list sequence nopublic public nozero zero help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete mhmail \
    'c/-/(body cc from subject help)/' \
    'n,-cc,$mha,'  \
    'n,-from,$mha,'  \
    'n/*/$mha/'

  complete mhpath \
    'c/-/(help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete msgchk \
    'c/-/(nodate date nonotify notify help)/' 

  complete msh \
    'c/-/(prompt noscan scan notopcur topcur help)/' 

  complete next \
    'c/-/(draft form moreproc nomoreproc length width showproc noshowproc header noheader help)/' \
    'c,+,$folders,'  \
    'n,-moreproc,c,'  \
    'n,-showproc,c,'  \
    'n,-form,f,'

  complete packf \
    'c/-/(file help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete pick \
    'c/-/(and or not lbrace rbrace cc date from search subject to othercomponent after before datefield sequence nopublic public nozero zero nolist list help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete prev \
    'c/-/(draft form moreproc nomoreproc length width showproc noshowproc header noheader help)/' \
    'c,+,$folders,'  \
    'n,-moreproc,c,'  \
    'n,-showproc,c,'  \
    'n,-form,f,'

  complete prompter \
    'c/-/(erase kill noprepend prepend norapid rapid nodoteof doteof help)/' 

  complete refile \
    'c/-/(draft nolink link nopreserve preserve src file help)/' \
    'c,+,$folders,'  \
    'n,-file,f,'\
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete rmf \
    'c/-/(nointeractive interactive help)/' \
    'c,+,$folders,'  

  complete rmm \
    'c/-/(help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete scan \
    'c/-/(noclear clear form format noheader header width noreverse reverse file help)/' \
    'c,+,$folders,'  \
    'n,-form,f,'\
    'n,-file,f,'\
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete send \
    'c/-/(alias draft draftfolder draftmessage nodraftfolder filter nofilter noformat format noforward forward nomsgid msgid nopush push noverbose verbose nowatch watch width help)/' \
    'n,-alias,f,'\
    'n,-filter,f,'

  complete show \
    'c/-/(draft form moreproc nomoreproc length width showproc noshowproc header noheader help)/' \
    'c,+,$folders,'  \
    'n,-moreproc,c,'  \
    'n,-showproc,c,'  \
    'n,-form,f,'\
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete sortm \
    'c/-/(datefield textfield notextfield limit nolimit noverbose verbose help)/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete vmh \
    'c/-/(prompt vmhproc novmhproc help)/' \
    'n,-vmhproc,c,'  

  complete whatnow \
    'c/-/(draftfolder draftmessage nodraftfolder editor noedit prompt help)/' 

  complete whom \
    'c/-/(alias nocheck check draft draftfolder draftmessage nodraftfolder help)/' \
    'n,-alias,f,'

  complete plum \
    'c/-/()/' \
    'c,+,$folders,'  \
    'n,*,`(mark | sed "s/:.*//" ; echo next cur prev first last)|tr " " "\012" | sort -u`,'

  complete mail \
    'c/-/()/' \
    'n/*/$mha/'

endif

#from Dan Nicolaescu <dann@ics.uci.edu>
if ( $?MODULESHOME ) then
  alias Compl_module \
    'find ${MODULEPATH:as/:/ /} -name .version -o -name .modulea\* -prune \
    -o -print | sed `echo "-e s@${MODULEPATH:as%:%/\*@@g -e s@%}/\*@@g"`'
  complete module \
    'p%1%(add load unload switch display avail use unuse update purge list \
	  clear help initadd initrm initswitch initlist initclear)%' \
    'n%{unl*,sw*,inits*}%`echo "$LOADEDMODULES:as/:/ /"`%' \
    'n%{lo*,di*,he*,inita*,initr*}%`eval Compl_module`%' \
    'N%{sw*,initsw*}%`eval Compl_module`%' \
    'C%-%(-append)%' \
    'n%{use,unu*,av*}%d%' \
    'n%-append%d%' \
    'C%[^-]*%`eval Compl_module`%'
endif

# from George Cox
complete acroread	'p/*/f:*.{pdf,PDF}/'
complete apachectl	'c/*/(start stop restart fullstatus status graceful \
			      configtest help)/'
complete appletviewer	'p/*/f:*.class/'
complete bison		'c/--/(debug defines file-prefix= fixed-output-files \
				help name-prefix= no-lines no-parser output= \
				token-table verbose version yacc)/' \
			'c/-/(b d h k l n o p t v y V)/' \
			'n/-b/f/' 'n/-o/f/' 'n/-p/f/'
complete bzcat		c/--/"(help test quiet verbose license version)"/ \
			c/-/"(h t L V -)"/ n/*/f:*.{bz2,tbz}/
complete bunzip2	c/--/"(help keep force test stdout quiet verbose \
				license version)"/ \
			c/-/"(h k f t c q v L V -)"/ \
			n/*/f:*.{bz2,tbz}/
complete bzip2		c/--/"(help decompress compress keep force test \
				stdout quiet verbose license version small)"/ \
			c/-/"(h d z k f t c q v L V s 1 2 3 4 5 6 7 8 9 -)"/ \
			n/{-d,--decompress}/f:*.{bz2,tbz}/ \
			N/{-d,--decompress}/f:*.{bz2,tbz}/ n/*/f:^*.{bz2,tbz}/
complete c++		'p/*/f:*.{c++,cxx,c,cc,C,cpp}/'
complete co		'p@1@`\ls -1a RCS | sed -e "s/\(.*\),v/\1/"`@'
complete crontab	'n/-u/u/'
complete camcontrol	'p/1/(cmd debug defects devlist eject inquiry \
			      modepage negotiate periphlist rescan reset \
			      start stop tags tur)/'
complete ctlinnd	'p/1/(addhist allow begin cancel changegroup \
			      checkfile drop feedinfo flush flushlogs go \
			      hangup logmode mode name newgroup param pause \
			      readers refile reject reload renumber reserve \
			      rmgroup send shutdown kill throttle trace \
			      xabort xexec)/'
complete cvs		'c/--/(help help-commands help-synonyms)/' \
			'p/1/(add admin annotate checkout commit diff edit \
			      editors export history import init log login \
			      logout rdiff release remove rtag status tag \
			      unedit update watch watchers)/' \
			'n/-a/(edit unedit commit all none)/' \
			'n/watch/(on off add remove)/'
complete svn	 	'C@file:///@`'"${HOME}/etc/tcsh/complete.d/svn"'`@@' \
			'n@ls@(file:/// svn+ssh:// svn://)@@' \
			'n@help@(add blame cat checkout cleanup commit copy \
				  delete export help import info list ls \
				  lock log merge mkdir move propdel propedit \
				  propget proplist propset resolved revert \
				  status switch unlock update)@' \
			'p@1@(add blame cat checkout cleanup commit copy \
			      delete export help import info list ls lock \
			      log merge mkdir move propdel propedit propget \
			      proplist propset resolved revert status switch \
			      unlock update)@'

complete cxx		'p/*/f:*.{c++,cxx,c,cc,C,cpp}/'
complete detex		'p/*/f:*.tex/'
complete edquota	'n/*/u/'
complete exec		'p/1/c/'
complete ghostview	'p/*/f:*.ps/'
complete gv		'p/*/f:*.ps/'
complete ifconfig	'p@1@`ifconfig -l`@' \
			'n/*/(range phase link netmask mtu vlandev vlan \
			    metric mediaopt down delete broadcast arp debug)/'
complete imake		'c/-I/d/'
complete ipfw		'p/1/(flush add delete list show zero)/' \
			'n/add/(allow permit accept pass deny drop reject \
				reset count skipto num divert port tee port)/'
complete javac		'p/*/f:*.java/'
complete ldif2ldbm	'n/-i/f:*.ldif/'
complete libtool	'c/--mode=/(compile execute finish install link \
				    uninstall)/' \
			'c/--/(config debug dry-run features finish help \
				quiet silent version mode=)/'
complete libtoolize	'c/--/(automake copy debug dry-run force help ltdl \
				ltdl-tar version)/'
complete links		'c/-/(assume-codepage async-dns download-dir \
			      format-cache-size ftp-proxy help http-proxy \
			      max-connections max-connections-to-host \
			      memory-cache-size receive-timeout retries \
			      unrestartable-receive-timeout version)/'
complete natd		c/-/'(alias_address config deny_incoming dynamic \
			      inport interface log log_denied log_facility \
			      outport outport port pptpalias proxy_only \
			      proxy_rule redirect_address redirect_port \
			      reverse same_ports unregistered_only use_sockets \
			      verbose)'/ \
			'n@-interface@`ifconfig -l`@'
complete netstat	'n@-I@`ifconfig -l`@'
complete objdump	'c/--/(adjust-vma= all-headers architecture= \
			      archive-headers debugging demangle disassemble \
			      disassemble-all disassemble-zeroes dynamic-reloc \
			      dynamic-syms endian= file-headers full-contents \
			      headers help info line-numbers no-show-raw-insn \
			      prefix-addresses private-headers reloc \
			      section-headers section=source stabs \
			      start-address= stop-address= syms target= \
			      version wide)/' \
			'c/-/(a h i f C d D p r R t T x s S l w)/'
complete xmodmap	'c/-/(display help grammar verbose quiet n e pm pk \
			      pke pp)/'
complete lynx		'c/-/(accept_all_cookies anonymous assume_charset= \
			      assume_local_charset= assume_unrec_charset= \
			      auth= base book buried_news cache= case cfg= \
			      child cookie_file= cookies core crawl \
			      debug_partial display= dump editor= emacskeys \
			      enable_scrollback error_file= force_html \
			      force_secure forms_options from ftp get_data \
			      head help hiddenlinks= historical homepage= \
			      image_links index= ismap link= localhost \
			      mime_header minimal newschunksize= \
			      newsmaxchunk= nobrowse nocc nocolor \
			      nofilereferer nolist nolog nopause noprint \
			      noredir noreferer nostatus number_links \
			      partial partial_thres pauth= popup post_data \
			      preparsed print pseudo_inlines raw realm \
			      reload restrictions= resubmit_posts rlogin \
			      selective show_cursor soft_dquotes source \
			      stack_dump startfile_ok tagsoup telnet term= \
			      tlog trace traversal underscore useragent= \
			      validate verbose version vikeys width=)/' \
			'c/(http|ftp)/$URLS/'
complete gmake		'c/{--directory=,--include-dir=}/d/' \
			'c/{--assume-new,--assume-old,--makefile,--new-file,--what-if,--file}/f/' \
			'c/--/(assume-new= assume-old= debug directory= \
			      dry-run environment-overrides file= help \
			      ignore-errors include-dir= jobs[=N] just-print \
			      keep-going load-average[=N] makefile= \
			      max-load[=N] new-file= no-builtin-rules \
			      no-keep-going no-print-directory old-file= \
			      print-data-base print-directory question quiet \
			      recon silent stop touch version \
			      warn-undefined-variables what-if=)/' \
			'n@*@`cat -s GNUMakefile Makefile makefile |& sed -n -e "/No such file/d" -e "s/^\([A-Za-z0-9-]*\):.*/\1/p"`@' \
			'n/=/f/' \
			'n/-f/f/'
complete mixer		p/1/'(vol bass treble synth pcm speaker mic cd mix \
			      pcm2 rec igain ogain line1 line2 line3)'/ \
			p@2@'`mixer $:-1 | awk \{\ print\ \$7\ \}`'@

complete mpg123		'c/--/(2to1 4to1 8bit aggressive au audiodevice auth \
			      buffer cdr check doublespeed equalizer frames \
			      gain halfspeed headphones left lineout list \
			      mix mono proxy quiet random rate reopen resync \
			      right scale shuffle single0 single1 skip \
			      speaker stdout stereo test verbose wav)/'
complete mysqladmin	'n/*/(create drop extended-status flush-hosts \
			      flush-logs flush-status flush-tables \
			      flush-privileges kill password ping \
			      processlist reload refresh shutdown status \
			      variables version)/'

complete mutt \
  "c@-f=@F:${HOME}/Mail/@" \
  n/-a/f/ \
  n/-F/f/ \
  n/-H/f/ \
  n/-s/x:'<subject line>'/ \
  n/-e/x:'<command>'/ \
  n@-b@'`cat "${HOME}/.muttrc-alias" | awk '"'"'{print $2 }'"'"\`@ \
  n@-c@'`cat "${HOME}/.muttrc-alias" | awk '"'"'{print $2 }'"'"\`@ \
  n@*@'`cat "${HOME}/.muttrc-alias" | awk '"'"'{print $2 }'"'"\`@

complete ndc	'n/*/(status dumpdb reload stats trace notrace \
		    querylog start stop restart )/'

complete nm \
  'c/--radix=/x:<radix: _o_ctal _d_ecimal he_x_adecimal>/' \
  'c/--target=/x:<bfdname>/' \
  'c/--format=/(bsd sysv posix)/n/' \
  'c/--/(debugsyms extern-only demangle dynamic print-armap \
	  print-file-name numeric-sort no-sort reverse-sort \
	  size-sort undefined-only portability target= radix= \
	  format= defined-only\ line-numbers no-demangle version \
	  help)//' \
  'n/*/f:^*.{h,c,cc,s,S}/'

complete nmap	'n@-e@`ifconfig -l`@' 'p/*/$hostnames/'
complete perldoc 	'n@*@`\ls -1 /usr/libdata/perl/5.*/pod | sed s%\\.pod.\*\$%%`@'
complete postfix    'n/*/(start stop reload abort flush check)/'
complete postmap	'n/1/(hash: regexp:)/' 'c/hash:/f/' 'c/regexp:/f/'
complete rcsdiff	'p@1@`\ls -1a RCS | sed -e "s/\(.*\),v/\1/"`@'
complete X		'c/-/(I a ac allowMouseOpenFail allowNonLocalModInDev \
		    allowNonLocalXvidtune ar1 ar2 audit auth bestRefresh \
		    bgamma bpp broadcast bs c cc class co core deferglyphs \
		    disableModInDev disableVidMode displayID dpi dpms f fc \
		    flipPixels fn fp gamma ggamma help indirect kb keeptty \
		    ld lf logo ls nolisten string noloadxkb nolock nopn \
		    once p pn port probeonly query quiet r rgamma s \
		    showconfig sp su t terminate to tst v verbose version \
		    weight wm x xkbdb xkbmap)/'
complete users      'c/--/(help version)/' 'p/1/x:"<accounting_file>"/'
complete vidcontrol	'p/1/(132x25 132x30 132x43 132x50 132x60 40x25 80x25 \
		    80x30 80x43 80x50 80x60 EGA_80x25 EGA_80x43 \
		    VESA_132x25 VESA_132x30 VESA_132x43 VESA_132x50 \
		    VESA_132x60 VESA_800x600 VGA_320x200 VGA_40x25 \
		    VGA_80x25 VGA_80x30 VGA_80x50 VGA_80x60)/'
complete vim	'n/*/f:^*.[oa]/'
complete where	'n/*/c/'
complete which	'n/*/c/'
complete wmsetbg	'c/-/(display D S a b c d e m p s t u w)/' \
		    'c/--/(back-color center colors dither help match \
		    maxscale parse scale smooth tile update-domain \
		    update-wmaker version workspace)/'
complete xdb	'p/1/c/'
complete xdvi	'c/-/(allowshell debug display expert gamma hushchars \
		    hushchecksums hushspecials install interpreter keep \
		    margins nogrey noinstall nomakepk noscan paper safer \
		    shrinkbuttonn thorough topmargin underlink version)/' \
		    'n/-paper/(a4 a4r a5 a5r)/' 'p/*/f:*.dvi/'
complete xlock	'c/-/(allowaccess allowroot debug description \
		    echokeys enablesaver grabmouse grabserver hide inroot \
		    install inwindow mono mousemotion nolock remote \
		    resetsaver sound timeelapsed use3d usefirst verbose \
		    wireframe background batchcount bg bitmap both3d \
		    count cycles delay delta3d display dpmsoff \
		    dpmsstandby dpmssuspend endCmd erasedelay erasemode \
		    erasetime fg font foreground geometry help \
		    icongeometry info invalid left3d lockdelay logoutCmd \
		    mailCmd mailIcon message messagefile messagefont \
		    messagesfile mode name ncolors nice nomailIcon none3d \
		    parent password planfont program resources right3d \
		    saturation size startCmd timeout username validate \
		    version visual)/' 'n/-mode/(ant atlantis ball bat \
		    blot bouboule bounce braid bubble bubble3d bug cage \
		    cartoon clock coral crystal daisy dclock decay deco \
		    demon dilemma discrete drift eyes fadeplot flag flame \
		    flow forest galaxy gears goop grav helix hop hyper \
		    ico ifs image invert julia kaleid kumppa lament laser \
		    life life1d life3d lightning lisa lissie loop lyapunov \
		    mandelbrot marquee matrix maze moebius morph3d \
		    mountain munch nose pacman penrose petal pipes puzzle \
		    pyro qix roll rotor rubik shape sierpinski slip sphere \
		    spiral spline sproingies stairs star starfish strange \
		    superquadrics swarm swirl tetris thornbird triangle \
		    tube turtle vines voters wator wire world worm xjack \
		    blank bomb random)/' 
complete xfig	'c/-/(display)/' 'p/*/f:*.fig/'
complete wget 	c/--/"(accept= append-output= background cache= \
		    continue convert-links cut-dirs= debug \
		    delete-after directory-prefix= domains= \
		    dont-remove-listing dot-style= exclude-directories= \
		    exclude-domains= execute= follow-ftp \
		    force-directories force-html glob= header= help \
		    http-passwd= http-user= ignore-length \
		    include-directories= input-file= level= mirror \
		    no-clobber no-directories no-host-directories \
		    no-host-lookup no-parent non-verbose \
		    output-document= output-file= passive-ftp \
		    proxy-passwd= proxy-user= proxy= quiet quota= \
		    recursive reject= relative retr-symlinks save-headers \
		    server-response span-hosts spider timeout= \
		    timestamping tries= user-agent= verbose version wait=)"/

# these from Tom Warzeka <tom@waz.cc>

# this one works but is slow and doesn't descend into subdirectories
# complete	cd	C@[./\$~]*@d@ \
#			p@1@'`\ls -1F . $cdpath | grep /\$ | sort -u`'@ n@*@n@

if ( -r /etc/shells ) then
    complete setenv	p@1@e@ n@DISPLAY@\$hosts@: n@SHELL@'`cat /etc/shells`'@
else
    complete setenv	p@1@e@ n@DISPLAY@\$hosts@:
endif
complete unsetenv	n/*/e/

set _maildir = /var/mail
if (-r "$HOME/.mailrc") then
    complete mail	c/-/"(e i f n s u v)"/ c/*@/\$hosts/ \
		    "c@+@F:$HOME/Mail@" C@[./\$~]@f@ n/-s/x:'<subject>'/ \
		    n@-u@T:$_maildir@ n/-f/f/ \
		    n@*@'`sed -n s/alias//p "$HOME/.mailrc" | \
		    tr -s " " "	" | cut -f 2`'@
else
    complete mail	c/-/"(e i f n s u v)"/ c/*@/\$hosts/ \
		    "c@+@F:$HOME/Mail@" C@[./\$~]@f@ n/-s/x:'<subject>'/ \
		    n@-u@T:$_maildir@ n/-f/f/ n/*/u/
endif
unset _maildir

if (! $?MANPATH) then
    if (-r /usr/share/man) then
	setenv MANPATH /usr/share/man:
    else
	setenv MANPATH /usr/man:
    endif
endif

if ($?traditional_complete) then
    # use of $MANPATH from Dan Nicolaescu <dann@ics.uci.edu>
    # use of 'find' adapted from Lubomir Host <host8@kepler.fmph.uniba.sk>
    complete man \
	'n@1@`set q = "$MANPATH:as%:%/man1 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.1.\*\$%%`@'\
	'n@2@`set q = "$MANPATH:as%:%/man2 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.2.\*\$%%`@'\
	'n@3@`set q = "$MANPATH:as%:%/man3 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.3.\*\$%%`@'\
	'n@4@`set q = "$MANPATH:as%:%/man4 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.4.\*\$%%`@'\
	'n@5@`set q = "$MANPATH:as%:%/man5 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.5.\*\$%%`@'\
	'n@6@`set q = "$MANPATH:as%:%/man6 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.6.\*\$%%`@'\
	'n@7@`set q = "$MANPATH:as%:%/man7 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.7.\*\$%%`@'\
	'n@8@`set q = "$MANPATH:as%:%/man8 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.8.\*\$%%`@'\
	'n@9@`set q = "$MANPATH:as%:%/man9 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.9.\*\$%%`@'\
	'n@0@`set q = "$MANPATH:as%:%/man0 %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.0.\*\$%%`@'\
	'n@n@`set q = "$MANPATH:as%:%/mann %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.n.\*\$%%`@'\
	'n@o@`set q = "$MANPATH:as%:%/mano %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.o.\*\$%%`@'\
	'n@l@`set q = "$MANPATH:as%:%/manl %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.l.\*\$%%`@'\
	'n@p@`set q = "$MANPATH:as%:%/manp %" ; \ls -1 $q |& sed -e s%^.\*:.\*\$%% -e s%\\.p.\*\$%%`@'\
	c@-@"(- f k M P s S t)"@ n@-f@c@ n@-k@x:'<keyword>'@ n@-[MP]@d@   \
	'N@-[MP]@`\ls -1 $:-1/man? |& sed -n s%\\..\\+\$%%p`@'            \
	'n@-[sS]@`\ls -1 $MANPATH:as%:% % |& sed -n s%^man%%p | sort -u`@'\
	'n@*@`find $MANPATH:as%:% % \( -type f -o -type l \) -printf "%f " |& sed -e "s%find: .*: No such file or directory%%" -e "s%\([^\.]\+\)\.\([^ ]*\) %\1 %g"`@'
	#n@*@c@ # old way -- commands only
else
    complete man	    n@1@'`\ls -1 /usr/man/man1 | sed s%\\.1.\*\$%%`'@ \
			n@2@'`\ls -1 /usr/man/man2 | sed s%\\.2.\*\$%%`'@ \
			n@3@'`\ls -1 /usr/man/man3 | sed s%\\.3.\*\$%%`'@ \
			n@4@'`\ls -1 /usr/man/man4 | sed s%\\.4.\*\$%%`'@ \
			n@5@'`\ls -1 /usr/man/man5 | sed s%\\.5.\*\$%%`'@ \
			n@6@'`\ls -1 /usr/man/man6 | sed s%\\.6.\*\$%%`'@ \
			n@7@'`\ls -1 /usr/man/man7 | sed s%\\.7.\*\$%%`'@ \
			n@8@'`\ls -1 /usr/man/man8 | sed s%\\.8.\*\$%%`'@ \
n@9@'`[ -r /usr/man/man9 ] && \ls -1 /usr/man/man9 | sed s%\\.9.\*\$%%`'@ \
n@0@'`[ -r /usr/man/man0 ] && \ls -1 /usr/man/man0 | sed s%\\.0.\*\$%%`'@ \
n@new@'`[ -r /usr/man/mann ] && \ls -1 /usr/man/mann | sed s%\\.n.\*\$%%`'@ \
n@old@'`[ -r /usr/man/mano ] && \ls -1 /usr/man/mano | sed s%\\.o.\*\$%%`'@ \
n@local@'`[ -r /usr/man/manl ] && \ls -1 /usr/man/manl | sed s%\\.l.\*\$%%`'@ \
n@public@'`[ -r /usr/man/manp ]&& \ls -1 /usr/man/manp | sed s%\\.p.\*\$%%`'@ \
	    c/-/"(- f k P s t)"/ n/-f/c/ n/-k/x:'<keyword>'/ n/-P/d/ \
	    N@-P@'`\ls -1 $:-1/man? | sed s%\\..\*\$%%`'@ n/*/c/
endif

complete ps	        c/-t/x:'<tty>'/ c/-/"(a c C e g k l S t u v w x)"/ \
		    n/-k/x:'<kernel>'/ N/-k/x:'<core_file>'/ n/*/x:'<PID>'/
complete compress	c/-/"(c f v b)"/ n/-b/x:'<max_bits>'/ n/*/f:^*.Z/
complete uncompress	c/-/"(c f v)"/                        n/*/f:*.Z/

complete uuencode	p/1/f/ p/2/x:'<decode_pathname>'/ n/*/n/
complete uudecode	c/-/"(f)"/ n/-f/f:*.{uu,UU}/ p/1/f:*.{uu,UU}/ n/*/n/

complete xhost	c/[+-]/\$hosts/ n/*/\$hosts/
complete xpdf	c/-/"(z g remote raise quit cmap rgb papercolor       \
			  eucjp t1lib freetype ps paperw paperh level1    \
			  upw fullscreen cmd q v h help)"/                \
		    n/-z/x:'<zoom (-5 .. +5) or "page" or "width">'/      \
		    n/-g/x:'<geometry>'/ n/-remote/x:'<name>'/            \
		    n/-rgb/x:'<number>'/ n/-papercolor/x:'<color>'/       \
		    n/-{t1lib,freetype}/x:'<font_type>'/                  \
		    n/-ps/x:'<PS_file>'/ n/-paperw/x:'<width>'/           \
		    n/-paperh/x:'<height>'/ n/-upw/x:'<password>'/        \
		    n/-/f:*.{pdf,PDF}/                                    \
		    N/-{z,g,remote,rgb,papercolor,t1lib,freetype,ps,paperw,paperh,upw}/f:*.{pdf,PDF}/ \
		    N/-/x:'<page>'/ p/1/f:*.{pdf,PDF}/ p/2/x:'<page>'/

complete tcsh	c/-D*=/'x:<value>'/ c/-D/'x:<name>'/ \
		    c/-/"(b c d D e f F i l m n q s t v V x X -version)"/ \
		    n/-c/c/ n/{-l,--version}/n/ n/*/'f:*.{,t}csh'/

complete rpm	c/--/"(query verify nodeps nofiles nomd5 noscripts    \
		    nogpg nopgp install upgrade freshen erase allmatches  \
		    notriggers repackage test rebuild recompile initdb    \
		    rebuilddb addsign resign querytags showrc setperms    \
		    setugids all file group package querybynumber qf      \
		    triggeredby whatprovides whatrequires changelog       \
		    configfiles docfiles dump filesbypkg info last list   \
		    provides queryformat requires scripts state triggers  \
		    triggerscripts allfiles badreloc excludepath checksig \
		    excludedocs force hash ignoresize ignorearch ignoreos \
		    includedocs justdb noorder oldpackage percent prefix  \
		    relocate replace-files replacepkgs buildroot clean    \
		    nobuild rmsource rmspec short-circuit sign target     \
		    help version quiet rcfile pipe dbpath root specfile)"/\
		    c/-/"(q V K i U F e ba bb bp bc bi bl bs ta tb tp tc  \
		    ti tl ts a f g p c d l R s h ? v vv -)"/              \
	    n/{-f,--file}/f/ n/{-g,--group}/g/ n/--pipe/c/ n/--dbpath/d/  \
	    n/--querybynumber/x:'<number>'/ n/--triggeredby/x:'<package>'/\
	    n/--what{provides,requires}/x:'<capability>'/ n/--root/d/     \
	    n/--{qf,queryformat}/x:'<format>'/ n/--buildroot/d/           \
	    n/--excludepath/x:'<oldpath>'/  n/--prefix/x:'<newpath>'/     \
	    n/--relocate/x:'<oldpath=newpath>'/ n/--target/x:'<platform>'/\
	    n/--rcfile/x:'<filelist>'/ n/--specfile/x:'<specfile>'/       \
	    n/{-[iUFep],--{install,upgrade,freshen,erase,package}}/f:*.rpm/

# these conform to the latest GNU versions available at press time ...
# updates by John Gotts <jgotts@engin.umich.edu>
if (-X emacs) then
  # TW note:  if your version of GNU Emacs supports the "--version" option,
  #           uncomment this line and comment the next to automatically
  #           detect the version, else set "_emacs_ver" to your version.
  #set _emacs_ver=`emacs --version | sed -e 's%GNU Emacs %%' -e q | cut -d . -f1-2`
  set _emacs_ver=21.3
  set _emacs_dir=`which emacs | sed s%/bin/emacs%%` 
  complete emacs	c/--/"(batch terminal display no-windows no-init-file \
			   user debug-init unibyte multibyte version help \
			   no-site-file funcall load eval insert kill)"/ \
		    c/-/"(t d nw q u f l -)"/ c/+/x:'<line_number>'/ \
		    n/{-t,--terminal}/x:'<terminal>'/ n/{-d,--display}/x:'<display>'/ \
		    n/{-u,--user}/u/ n/{-f,--funcall}/x:'<lisp_function>'/ \
		    n@{-l,--load}@F:$_emacs_dir/share/emacs/$_emacs_ver/lisp@ \
		    n/--eval/x:'<expression>'/ n/--insert/f/ n/*/f:^*[\#~]/
  unset _emacs_ver _emacs_dir
endif

complete gzcat	c/--/"(force help license quiet version)"/ \
		    c/-/"(f h L q V -)"/ n/*/f:*.{gz,Z,z,zip}/
complete gzip	c/--/"(stdout to-stdout decompress uncompress \
		    force help list license no-name quiet recurse \
		    suffix test verbose version fast best)"/ \
		    c/-/"(c d f h l L n q r S t v V 1 2 3 4 5 6 7 8 9 -)"/\
		    n/{-S,--suffix}/x:'<file_name_suffix>'/ \
		    n/{-d,--{de,un}compress}/f:*.{gz,Z,z,zip,taz,tgz}/ \
		    N/{-d,--{de,un}compress}/f:*.{gz,Z,z,zip,taz,tgz}/ \
		    n/*/f:^*.{gz,Z,z,zip,taz,tgz}/
complete {gunzip,ungzip} c/--/"(stdout to-stdout force help list license \
		    no-name quiet recurse suffix test verbose version)"/ \
		    c/-/"(c f h l L n q r S t v V -)"/ \
		    n/{-S,--suffix}/x:'<file_name_suffix>'/ \
		    n/*/f:*.{gz,Z,z,zip,taz,tgz}/
complete zgrep	c/-*A/x:'<#_lines_after>'/ c/-*B/x:'<#_lines_before>'/\
		    c/-/"(A b B c C e f h i l n s v V w x)"/ \
		    p/1/x:'<limited_regular_expression>'/ N/-*e/f/ \
		    n/-*e/x:'<limited_regular_expression>'/ n/-*f/f/ n/*/f/
complete zegrep	c/-*A/x:'<#_lines_after>'/ c/-*B/x:'<#_lines_before>'/\
		    c/-/"(A b B c C e f h i l n s v V w x)"/ \
		    p/1/x:'<full_regular_expression>'/ N/-*e/f/ \
		    n/-*e/x:'<full_regular_expression>'/ n/-*f/f/ n/*/f/
complete zfgrep	c/-*A/x:'<#_lines_after>'/ c/-*B/x:'<#_lines_before>'/\
		    c/-/"(A b B c C e f h i l n s v V w x)"/ \
		    p/1/x:'<fixed_string>'/ N/-*e/f/ \
		    n/-*e/x:'<fixed_string>'/ n/-*f/f/ n/*/f/
complete znew	c/-/"(f t v 9 P K)"/ n/*/f:*.Z/
complete zmore	n/*/f:*.{gz,Z,z,zip}/
complete zfile	n/*/f:*.{gz,Z,z,zip,taz,tgz}/
complete ztouch	n/*/f:*.{gz,Z,z,zip,taz,tgz}/
complete zforce	n/*/f:^*.{gz,tgz}/

complete dcop 'p/1/`$:0`/ /' \
    'p/2/`$:0 $:1 | awk \{print\ \$1\}`/ /' \
    'p/3/`$:0 $:1 $:2 | sed "s%.* \(.*\)(.*%\1%"`/ /'


complete grep	c/-*A/x:'<#_lines_after>'/ c/-*B/x:'<#_lines_before>'/\
		    c/--/"(extended-regexp fixed-regexp basic-regexp \
		    regexp file ignore-case word-regexp line-regexp \
		    no-messages revert-match version help byte-offset \
		    line-number with-filename no-filename quiet silent \
		    text directories recursive files-without-match \
		    files-with-matches count before-context after-context \
		    context binary unix-byte-offsets)"/ \
		    c/-/"(A a B b C c d E e F f G H h i L l n q r s U u V \
			    v w x)"/ \
		    p/1/x:'<limited_regular_expression>'/ N/-*e/f/ \
		    n/-*e/x:'<limited_regular_expression>'/ n/-*f/f/ n/*/f/
complete egrep	c/-*A/x:'<#_lines_after>'/ c/-*B/x:'<#_lines_before>'/\
		    c/--/"(extended-regexp fixed-regexp basic-regexp \
		    regexp file ignore-case word-regexp line-regexp \
		    no-messages revert-match version help byte-offset \
		    line-number with-filename no-filename quiet silent \
		    text directories recursive files-without-match \
		    files-with-matches count before-context after-context \
		    context binary unix-byte-offsets)"/ \
		    c/-/"(A a B b C c d E e F f G H h i L l n q r s U u V \
			    v w x)"/ \
		    p/1/x:'<full_regular_expression>'/ N/-*e/f/ \
		    n/-*e/x:'<full_regular_expression>'/ n/-*f/f/ n/*/f/
complete fgrep	c/-*A/x:'<#_lines_after>'/ c/-*B/x:'<#_lines_before>'/\
		    c/--/"(extended-regexp fixed-regexp basic-regexp \
		    regexp file ignore-case word-regexp line-regexp \
		    no-messages revert-match version help byte-offset \
		    line-number with-filename no-filename quiet silent \
		    text directories recursive files-without-match \
		    files-with-matches count before-context after-context \
		    context binary unix-byte-offsets)"/ \
		    c/-/"(A a B b C c d E e F f G H h i L l n q r s U u V \
			    v w x)"/ \
		    p/1/x:'<fixed_string>'/ N/-*e/f/ \
		    n/-*e/x:'<fixed_string>'/ n/-*f/f/ n/*/f/

complete sed	c/--/"(quiet silent version help expression file)"/   \
		    c/-/"(n V e f -)"/ n/{-e,--expression}/x:'<script>'/  \
		    n/{-f,--file}/f:*.sed/ N/-{e,f,-{file,expression}}/f/ \
		    n/-/x:'<script>'/ N/-/f/ p/1/x:'<script>'/ p/2/f/

complete users	c/--/"(help version)"/ p/1/x:'<accounting_file>'/
complete who	c/--/"(heading idle count mesg message writable help \
		    version)"/ c/-/"(H i m q s T w u -)"/ \
		    p/1/x:'<accounting_file>'/ n/am/"(i)"/ n/are/"(you)"/

complete chown	c/--/"(changes dereference no-dereference silent \
		    quiet reference recursive verbose help version)"/ \
		    c/-/"(c f h R v -)"/ C@[./\$~]@f@ c/*[.:]/g/ \
		    n/-/u/: p/1/u/: n/*/f/
complete chgrp	c/--/"(changes no-dereference silent quiet reference \
		    recursive verbose help version)"/ \
		    c/-/"(c f h R v -)"/ n/-/g/ p/1/g/ n/*/f/
complete chmod	c/--/"(changes silent quiet verbose reference \
		    recursive help version)"/ c/-/"(c f R v)"/
complete df		c/--/"(all block-size human-readable si inodes \
		    kilobytes local megabytes no-sync portability sync \
		    type print-type exclude-type help version)"/ \
		    c/-/"(a H h i k l m P T t v x)"/
complete du		c/--/"(all block-size bytes total dereference-args \
		    human-readable si kilobytes count-links dereference \
		    megabytes separate-dirs summarize one-file-system \
		    exclude-from exclude max-depth help version"/ \
		    c/-/"(a b c D H h k L l m S s X x)"/

complete cat	c/--/"(number-nonblank number squeeze-blank show-all \
		    show-nonprinting show-ends show-tabs help version)"/ \
		    c/-/"(A b E e n s T t u v -)"/ n/*/f/
complete mv		c/--/"(backup force interactive update verbose suffix \
		    version-control help version)"/ \
		    c/-/"(b f i S u V v -)"/ \
		    n/{-S,--suffix}/x:'<suffix>'/ \
		    n/{-V,--version-control}/"(t numbered nil existing \
		    never simple)"/ n/-/f/ N/-/d/ p/1/f/ p/2/d/ n/*/f/
complete cp		c/--/"(archive backup no-dereference force \
		    interactive link preserve parents sparse recursive \
		    symbolic-link suffix update verbose version-control \
		    one-file-system help version)"/ \
		    c/-/"(a b d f i l P p R r S s u V v x -)"/ \
		    n/-*r/d/ n/{-S,--suffix}/x:'<suffix>'/ \
		    n/{-V,--version-control}/"(t numbered nil existing \
		    never simple)"/ n/-/f/ N/-/d/ p/1/f/ p/2/d/ n/*/f/
complete ln		c/--/"(backup directory force no-dereference \
		    interactive symbolic suffix verbose version-control \
		    help version)"/ \
		    c/-/"(b d F f i n S s V v -)"/ \
		    n/{-S,--suffix}/x:'<suffix>'/ \
		    n/{-V,--version-control}/"(t numbered nil existing \
		    never simple)"/ n/-*/f/ N/-*/x:'<link_name>'/ \
		    p/1/f/ p/2/x:'<link_name>'/
complete touch	c/--/"(date reference time help version)"/ \
		    c/-/"(a c d f m r t -)"/ \
		    n/{-d,--date}/x:'<date_string>'/ \
		    c/--time/"(access atime mtime modify use)"/ \
		    n/{-r,--file}/f/ n/-t/x:'<time_stamp>'/ n/*/f/
complete mkdir	c/--/"(mode parents verbose help version)"/ \
		    c/-/"(p m -)"/ \
		    n/{-m,--mode}/x:'<mode>'/ n/*/d/
complete rmdir	c/--/"(ignore-fail-on-non-empty parents verbose help \
		    version)"/ c/-/"(p -)"/ n/*/d/
complete env 	'c/*=/f/' 'p/1/e/=/' 'p/2/c/'

complete tar	c/-[Acru]*/"(b B C f F g G h i l L M N o P \
		    R S T v V w W X z Z)"/ \
		    c/-[dtx]*/"( B C f F g G i k K m M O p P \
		    R s S T v w x X z Z)"/ \
		    p/1/"(A c d r t u x -A -c -d -r -t -u -x \
		    --catenate --concatenate --create --diff --compare \
		    --delete --append --list --update --extract --get \
		    --help --version)"/ \
		    c/--/"(catenate concatenate create diff compare \
		    delete append list update extract get atime-preserve \
		    block-size read-full-blocks directory checkpoint file \
		    force-local info-script new-volume-script incremental \
		    listed-incremental dereference ignore-zeros \
		    ignore-failed-read keep-old-files starting-file \
		    one-file-system tape-length modification-time \
		    multi-volume after-date newer old-archive portability \
		    to-stdout same-permissions preserve-permissions \
		    absolute-paths preserve record-number remove-files \
		    same-order preserve-order same-owner sparse \
		    files-from null totals verbose label version \
		    interactive confirmation verify exclude exclude-from \
		    compress uncompress gzip ungzip use-compress-program \
		    block-compress help version)"/ \
		    c/-/"(b B C f F g G h i k K l L m M N o O p P R s S \
		    T v V w W X z Z 0 1 2 3 4 5 6 7 -)"/ \
		    C@/dev@f@ \
		    n/-c*f/x:'<new_tar_file, device_file, or "-">'/ \
		    n/{-[Adrtux]j*f,--file}/f:*.{tar.bz2,tbz}/ \
		    n/{-[Adrtux]z*f,--file}/f:*.{tar.gz,tgz}/ \
		    n/{-[Adrtux]Z*f,--file}/f:*.{tar.Z,taz}/ \
		    n/{-[Adrtux]*f,--file}/f:*.tar/ \
		    N/{-xj*f,--file}/'`tar -tjf $:-1`'/ \
		    N/{-xz*f,--file}/'`tar -tzf $:-1`'/ \
		    N/{-xZ*f,--file}/'`tar -tZf $:-1`'/ \
		    N/{-x*f,--file}/'`tar -tf $:-1`'/ \
		    n/--use-compress-program/c/ \
		    n/{-b,--block-size}/x:'<block_size>'/ \
		    n/{-V,--label}/x:'<volume_label>'/ \
		    n/{-N,--{after-date,newer}}/x:'<date>'/ \
		    n/{-L,--tape-length}/x:'<tape_length_in_kB>'/ \
		    n/{-C,--directory}/d/ \
		    N/{-C,--directory}/'`\ls $:-1`'/ \
		    n/-[0-7]/"(l m h)"/

switch ( "$OSTYPE" )
case linux:
  # Linux filesystems
  complete  mount	c/-/"(a f F h l n o r s t U v V w)"/ n/-[hV]/n/ \
		    n/-o/x:'<options>'/ n/-t/x:'<vfstype>'/ \
		    n/-L/x:'<label>'/ n/-U/x:'<uuid>'/ \
		    n@*@'`grep -v "^#" /etc/fstab | tr -s " " "	 " | cut -f 2`'@
  complete umount	c/-/"(a h n r t v V)"/ n/-t/x:'<vfstype>'/ \
		      n/*/'`mount | cut -d " " -f 3`'/
  breaksw
case sunos*:
case solaris:
  # Solaris filesystems
  complete  mount	c/-/"(a F m o O p r v V)"/ n/-p/n/ n/-v/n/ \
		    n/-o/x:'<FSType_options>'/ \
		    n@-F@'`\ls -1 /usr/lib/fs`'@ \
		    n@*@'`grep -v "^#" /etc/vfstab | tr -s " " "	 " | cut -f 3`'@
  complete umount	c/-/"(a o V)"/ n/-o/x:'<FSType_options>'/ \
		    n/*/'`mount | cut -d " " -f 1`'/
  complete  mountall	c/-/"(F l r)"/ n@-F@'`\ls -1 /usr/lib/fs`'@
  complete umountall	c/-/"(F h k l r s)"/ n@-F@'`\ls -1 /usr/lib/fs`'@ \
		    n/-h/'`df -k | cut -s -d ":" -f 1 | sort -u`'/
  breaksw
case cygwin:
  # Cygwin mounts
  complete  mount	c/-/"(b c f h m o p s t u v x E X)"/ n/-[hmpv]/n/ \
		    n/-c/x:'/'/ \
		    n/-o/"(user system binary text exec notexec cygexec nosuid managed)"/ \
		    n@*@'`mount -p | tail -1 | cut -d " " -f 1 | xargs ls -1 | awk '"'"'{print $1":/"; } END{print "//";}'"'"'`'@
  complete umount	c/-/"(A c h s S u U v)"/ n/-[AhSUv]/n/ \
		    n@*@'`mount | grep -v noumount | cut -d " " -f 3`'@
  breaksw
endsw

# these deal with NIS (formerly YP); if it's not running you don't need 'em
if (-X domainname) then
  set _domain = "`domainname`"
  set _ypdir  = /var/yp	# directory where NIS (YP) maps are kept
  if ("$_domain" != "" && "$_domain" != "noname") then
    complete domainname p@1@D:$_ypdir@" " n@*@n@
    complete ypcat	    c@-@"(d k t x)"@ n@-x@n@ n@-d@D:$_ypdir@" " \
			N@-d@\`\\ls\ -1\ $_ypdir/\$:-1\ \|\ sed\ -n\ s%\\\\.pag\\\$%%p\`@ \
			n@*@\`\\ls\ -1\ $_ypdir/$_domain\ \|\ sed\ -n\ s%\\\\.pag\\\$%%p\`@
    complete ypmatch    c@-@"(d k t x)"@ n@-x@n@ n@-d@D:$_ypdir@" " \
			N@-d@x:'<key ...>'@ n@-@x:'<key ...>'@ p@1@x:'<key ...>'@ \
			n@*@\`\\ls\ -1\ $_ypdir/$_domain\ \|\ sed\ -n\ s%\\\\.pag\\\$%%p\`@
    complete ypwhich    c@-@"(d m t x V1 V2)"@ n@-x@n@ n@-d@D:$_ypdir@" " \
			n@-m@\`\\ls\ -1\ $_ypdir/$_domain\ \|\ sed\ -n\ s%\\\\.pag\\\$%%p\`@ \
			N@-m@n@ n@*@\$hosts@
  endif
  unset _domain _ypdir
endif

complete make \
    'n/-f/f/' \
    'c/*=/f/' \
    'n@*@`cat -s GNUmakefile Makefile makefile |& sed -n -e "/No such file/d" -e "/^[^     #].*:/s/:.*//p"`@'

if ( -f /etc/printcap ) then
    set printers=(`sed -n -e "/^[^     #].*:/s/:.*//p" /etc/printcap`)

    complete lpr    'c/-P/$printers/'
    complete lpq    'c/-P/$printers/'
    complete lprm   'c/-P/$printers/'
    complete lpquota        'p/1/(-Qprlogger)/' 'c/-P/$printers/'
    complete dvips  'c/-P/$printers/' 'n/-o/f:*.{ps,PS}/' 'n/*/f:*.dvi/'
    complete dvilj	'p/*/f:*.dvi/'
endif

# From Alphonse Bendt
complete ant \
     'n/-f/f:*.xml/' \
	  'n@*@`cat build.xml | sed -n -e "s/[ \t]*<target[\t\n]*name=.\([a-zA-Z0-9_:]*\).*/\1/p"`@'

if ($?P4CLIENT && -X perl) then
    # This is from Greg Allen.
    set p4cmds=(add branch branches commands change changes client clients \
	counter counters delete depot depots describe diff diff2 \
	edit filelog files fix fixes fstat group groups have help \
	info integrate integrated job jobs jobspec label labels \
	labelsync lock obliterate opened passwd print protect rename \
	reopen resolve resolved revert review reviews set submit \
	sync triggers unlock user users verify where)
    complete p4 'p/1/$p4cmds/' 'n/help/$p4cmds/' \
	'n%{-l,label}%`p4 labels | sed "s/Label \([^ ]*\) .*/\1/"`%' \
	'n%-t%`p4 $:1s | sed "s/[^ ]* \([^ ]*\) .*/\1/"`%' \
	'c%*@%`p4 labels | sed "s/Label \([^ ]*\) .*/\1/"`%' \
	'c@//*/*@`p4 files $:-0... |& perl -nle "m%\Q$:-0\E([^#][^/# ] \
	*)%;print "\$"1 if \\\!/no such/&&\!"\$"h{"\$"1}++"`@@' \
	'c@//@`p4 depots | sed "s/Depot \([^ ]*\) .*/\1/"`@/@'
endif


if (! $?traditional_complete) then
    uncomplete vi
    uncomplete vim
    complete {vi,vim,gvim,nvi,elvis} 	n/*/f:^*.{o,a,so,sa,aux,dvi,log,fig,bbl,blg,bst,idx,ilg,ind,toc}/
    complete {ispell,spell,spellword}	'n@-d@`ls /usr/lib/ispell/*.aff | sed -e "s/\.aff//" `@' 'n/*/f:^*.{o,a,so,sa,aux,dvi,log,fig,bbl,blg,bst,idx,ilg,ind,toc}/'
    complete elm	'n/-[Ai]/f/' 'c@=@F:$HOME/Mail/@' 'n/-s/x:\<subject\>/'
    complete ncftp	'n@*@`sed -e '1,2d' $HOME/.ncftp/bookmarks | cut -f 1,2 -d "," | tr "," "\012" | sort | uniq ` '@
    complete bibtex	'n@*@`ls *.aux | sed -e "s/\.aux//"`'@
    complete dvi2tty	n/*/f:*.dvi/	# Only files that match *.dvi
    uncomplete gv
    uncomplete ghostview
    complete {gv,ghostview}	'n/*/f:*.{ps,eps,epsi}/'
    complete enscript \
	    'c/--/(columns= pages= header= no-header truncate-lines \
		    line-numbers setpagedevice= escapes font= \
		    header-font= fancy-header no-job-header \
		    highlight-bars indent= filter= borders page-prefeed \
		    no-page-prefeed lineprinter lines-per-page= mail \
		    media= copies= newline= output= missing-characters \
		    printer= quiet silent landscape portrait \
		    baselineskip= statusdict= title= tabsize= underlay= \
		    verbose version encoding pass-through download-font= \
		    filter-stdin= help highlight-bar-gray= list-media \
		    list-options non-printable-format= page-label-format= \
		    printer-options= ul-angle= ul-font= ul-gray= \
		    ul-position= ul-style= \
		 )/'
endif

complete dpkg \
	    'c/--{admindir,instdir,root}=/d/' \
	    'c/--debug=/n/' \
	    'c/--{admindir,debug,instdir,root}/(=)//' \
	    'c/--/(admindir= debug= instdir= root= \
		    assert-support-predepends assert-working-epoch \
		    audit auto-deconfigure clear-avail \
		    compare-versions configure contents control \
		    extract force-bad-path field \
		    force-configure-any force-conflicts \
		    force-depends force-depends-version force-help \
		    force-hold force-non-root \
		    force-overwrite-diverted \
		    force-remove-essential force-remove-reinstreq \
		    forget-old-unavail fsys-tarfile get-selections \
		    help ignore-depends info install largemem \
		    license list listfiles merge-avail no-act \
		    pending predep-package print-architecture \
		    print-gnu-build-architecture \
		    print-installation-architecture print-avail \
		    purge record-avail recursive refuse-downgrade \
		    remove search set-selections selected-only \
		    skip-same-version smallmem status unpack \
		    update-avail version vextract \
		  )//' \
	    'n/{-l}/`dpkg -l|awk \{print\ \$2\}`/' \
	    'n/*/f:*.deb'/
complete dpkg-deb 	   'c/--{build}=/d/' \
		       'c/--/(build contents info field control extract \
			     vextract fsys-tarfile help version \
			     license)//' \
		       'n/*/f:*.deb/'
complete apt-get \
	    'c/--/(build config-file diff-only download-only \
	       fix-broken fix-missing force-yes help ignore-hold no-download \
	       no-upgrade option print-uris purge reinstall quiet simulate \
	       show-upgraded target-release tar-only version yes )/' \
	    'c/-/(b c= d f h m o= q qq s t x y )/' \
	    'n/{source,build-dep}/x:<pkgname>/' \
	    'n/{remove}/`dpkg -l|grep ^ii|awk \{print\ \$2\}`/' \
	    'n/{install}/`apt-cache pkgnames | sort`/' \
	    'C/*/(update upgrade dselect-upgrade source \
	       build-dep check clean autoclean install remove)/'
complete apt-cache \
	    'c/--/(all-versions config-file generate full help important \
	    names-only option pkg-cache quiet recurse src-cache version )/' \
	    'c/-/(c= h i o= p= q s= v)/' \
	    'n/{search}/x:<regex>/' \
	    'n/{pkgnames,policy,show,showpkg,depends,dotty}/`apt-cache pkgnames | sort`/' \
	    'C/*/(add gencaches showpkg stats dump dumpavail unmet show \
	    search depends pkgnames dotty policy )/'

switch ( "${OSTYPE}" )
case FreeBSD:
  set commands=()
  foreach p (fast force one quiet "")
    foreach c (enabled poll rcvar reload restart start status stop)
      set commands=($commands $p$c)
    end
  end
  complete service \
    n/-R/n/ \
    n/-e/n/ \
    n/-l/n/ \
    n/-r/n/ \
    c/-/"(R e l r v)"/ \
    p/2/"($commands)"/ \
    p@1@'`service -l`'@
  unset commands c p
  breaksw
case linux:
  if ( -d /etc/init.d ) then
    set rcdir=/etc/init.d/
  else
    set rcdir=/etc/rc.d/
  endif
  complete service \
    p/2/"(--full-restart force-reload reload restart start stop status)"/ \
    c/--/"(help status-all version)"/ \
    c/-/"(- h)"/ \
    p@1@F:$rcdir@
  unset rcdir
  breaksw
endsw

if ( $?_unset_noglob ) unset noglob _unset_noglob

end:
unset _has_complete
onintr
