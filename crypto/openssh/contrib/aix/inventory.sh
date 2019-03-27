#!/bin/sh
#
# inventory.sh
#
# Originally written by Ben Lindstrom, modified by Darren Tucker to use perl
# This file is placed into the public domain.
#
# This will produce an AIX package inventory file, which looks like:
#
# /usr/local/bin:
#          class=apply,inventory,openssh
#          owner=root
#          group=system
#          mode=755
#          type=DIRECTORY
# /usr/local/bin/slogin:
#          class=apply,inventory,openssh
#          owner=root
#          group=system
#          mode=777
#          type=SYMLINK
#          target=ssh
# /usr/local/share/Ssh.bin:
#          class=apply,inventory,openssh
#          owner=root
#          group=system
#          mode=644
#          type=FILE
#          size=VOLATILE
#          checksum=VOLATILE

find . ! -name . -print | perl -ne '{
	chomp;
	if ( -l $_ ) {
		($dev,$ino,$mod,$nl,$uid,$gid,$rdev,$sz,$at,$mt,$ct,$bsz,$blk)=lstat;
	} else {
		($dev,$ino,$mod,$nl,$uid,$gid,$rdev,$sz,$at,$mt,$ct,$bsz,$blk)=stat;
	}

	# Start to display inventory information
	$name = $_;
	$name =~ s|^.||;	# Strip leading dot from path
	print "$name:\n";
	print "\tclass=apply,inventory,openssh\n";
	print "\towner=root\n";
	print "\tgroup=system\n";
	printf "\tmode=%lo\n", $mod & 07777;	# Mask perm bits
	
	if ( -l $_ ) {
		# Entry is SymLink
		print "\ttype=SYMLINK\n";
		printf "\ttarget=%s\n", readlink($_);
	} elsif ( -f $_ ) {
		# Entry is File
		print "\ttype=FILE\n";
		print "\tsize=$sz\n";
		print "\tchecksum=VOLATILE\n";
	} elsif ( -d $_ ) {
		# Entry is Directory
		print "\ttype=DIRECTORY\n";
	}
}'
