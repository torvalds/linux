#!/usr/bin/perl -w
#
# @(#) mk2knr.pl - generates a perl script that converts lexemes to K&R-style
#
# How to use this script:
#  - In the busybox directory type 'examples/mk2knr.pl files-to-convert'
#  - Review the 'convertme.pl' script generated and remove / edit any of the
#    substitutions in there (please especially check for false positives)
#  - Type './convertme.pl same-files-as-before'
#  - Compile and see if it works
#
# BUGS: This script does not ignore strings inside comments or strings inside
# quotes (it probably should).

# set this to something else if you want
$convertme = 'convertme.pl';

# internal-use variables (don't touch)
$convert = 0;
%converted = ();

# if no files were specified, print usage
die "usage: $0 file.c | file.h\n" if scalar(@ARGV) == 0;

# prepare the "convert me" file
open(CM, ">$convertme") or die "convertme.pl $!";
print CM "#!/usr/bin/perl -p -i\n\n";

# process each file passed on the cmd line
while (<>) {

	# if the line says "getopt" in it anywhere, we don't want to muck with it
	# because option lists tend to include strings like "cxtzvOf:" which get
	# matched by the "check for mixed case" regexps below
	next if /getopt/;

	# tokenize the string into just the variables
	while (/([a-zA-Z_][a-zA-Z0-9_]*)/g) {
		$var = $1;

		# ignore the word "BusyBox"
		next if ($var =~ /BusyBox/);

		# this checks for javaStyle or szHungarianNotation
		$convert++ if ($var =~ /^[a-z]+[A-Z][a-z]+/);

		# this checks for PascalStyle
		$convert++ if ($var =~ /^[A-Z][a-z]+[A-Z][a-z]+/);

		# if we want to add more checks, we can add 'em here, but the above
		# checks catch "just enough" and not too much, so prolly not.

		if ($convert) {
			$convert = 0;

			# skip ahead if we've already dealt with this one
			next if ($converted{$var});

			# record that we've dealt with this var
			$converted{$var} = 1;

			print CM "s/\\b$var\\b/"; # more to come in just a minute

			# change the first letter to lower-case
			$var = lcfirst($var);

			# put underscores before all remaining upper-case letters
			$var =~ s/([A-Z])/_$1/g;

			# now change the remaining characters to lower-case
			$var = lc($var);

			print CM "$var/g;\n";
		}
	}
}

# tidy up and make the $convertme script executable
close(CM);
chmod 0755, $convertme;

# print a helpful help message
print "Done. Scheduled name changes are in $convertme.\n";
print "Please review/modify it and then type ./$convertme to do the search & replace.\n";
