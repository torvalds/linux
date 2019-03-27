=head1 NAME

Mdoc - perl module to parse Mdoc macros

=head1 SYNOPSIS

    use Mdoc qw(ns pp soff son stoggle mapwords);

See mdoc2man and mdoc2texi for code examples.

=head1 FUNCTIONS

=over 4

=item def_macro( NAME, CODE, [ raw => 1, greedy => 1, concat_until => '.Xx' ] )

Define new macro. The CODE reference will be called by call_macro(). You can
have two distinct definitions for and inline macro and for a standalone macro
(i. e. 'Pa' and '.Pa').

The CODE reference is passed a list of arguments and is expected to return list
of strings and control characters (see C<CONSTANTS>).

By default the surrouding "" from arguments to macros are removed, use C<raw>
to disable this.

Normaly CODE reference is passed all arguments up to next nested macro. Set
C<greedy> to to pass everything up to the end of the line.

If the concat_until is present, the line is concated until the .Xx macro is
found. For example the following macro definition

    def_macro('.Oo', gen_encloser(qw([ ]), concat_until => '.Oc' }
    def_macro('.Cm', sub { mapwords {'($_)'} @_ } }

and the following input

    .Oo
    .Cm foo |
    .Cm bar |
    .Oc

results in [(foo) | (bar)]

=item get_macro( NAME )

Returns a hash reference like:

    { run => CODE, raw => [1|0], greedy => [1|0] }

Where C<CODE> is the CODE reference used to define macro called C<NAME>

=item parse_line( INPUT, OUTPUT_CODE, PREPROCESS_CODE )

Parse a line from the C<INPUT> filehandle. If a macro was detected it returns a
list (MACRO_NAME, @MACRO_ARGS), otherwise it calls the C<OUTPUT_CODE>, giving
caller a chance to modify line before printing it. If C<PREPROCESS_CODE> is
defined it calls it prior to passing argument to a macro, giving caller a
chance to alter them.  if EOF was reached undef is returned.

=item call_macro( MACRO, ARGS, ... )

Call macro C<MACRO> with C<ARGS>. The CODE reference for macro C<MACRO> is
called and for all the nested macros. Every called macro returns a list which
is appended to return value and returned when all nested macros are processed.
Use to_string() to produce a printable string from the list.

=item to_string ( LIST )

Processes C<LIST> returned from call_macro() and returns formatted string.

=item mapwords BLOCK ARRAY

This is like perl's map only it calls BLOCK only on elements which are not
punctuation or control characters.

=item space ( ['on'|'off] )

Turn spacing on or off. If called without argument it returns the current state.

=item gen_encloser ( START, END )

Helper function for generating macros that enclose their arguments.
    gen_encloser(qw({ }));
returns
    sub { '{', ns, @_, ns, pp('}')}

=item set_Bl_callback( CODE , DEFS )

This module implements the Bl/El macros for you. Using set_Bl_callback you can
provide a macro definition that should be executed on a .Bl call.

=item set_El_callback( CODE , DEFS )

This module implements the Bl/El macros for you. Using set_El_callback you can
provide a macro definition that should be executed on a .El call.

=item set_Re_callback( CODE )

The C<CODE> is called after a Rs/Re block is done. With a hash reference as a
parameter, describing the reference.

=back 

=head1 CONSTANTS

=over 4

=item ns

Indicate 'no space' between to members of the list.

=item pp ( STRING )

The string is 'punctuation point'. It means that every punctuation
preceeding that element is put behind it. 

=item soff

Turn spacing off.

=item son

Turn spacing on.

=item stoggle

Toogle spacing.

=item hs

Print space no matter spacing mode.

=back

=head1 TODO

* The concat_until only works with standalone macros. This means that
    .Po blah Pc
will hang until .Pc in encountered.

* Provide default macros for Bd/Ed

* The reference implementation is uncomplete

=cut

package Mdoc;
use strict;
use warnings;
use List::Util qw(reduce);
use Text::ParseWords qw(quotewords);
use Carp;
use Exporter qw(import);
our @EXPORT_OK = qw(ns pp soff son stoggle hs mapwords gen_encloser nl);

use constant {
    ns      => ['nospace'],
    soff    => ['spaceoff'],
    son     => ['spaceon'],
    stoggle => ['spacetoggle'],
    hs      => ['hardspace'],
};

sub pp { 
    my $c = shift;
    return ['pp', $c ];
}
sub gen_encloser {
    my ($o, $c) = @_;
    return sub { ($o, ns, @_, ns, pp($c)) };
}

sub mapwords(&@) {
    my ($f, @l) = @_;
    my @res;
    for my $el (@l) {
        local $_ = $el;
        push @res, $el =~ /^(?:[,\.\{\}\(\):;\[\]\|])$/ || ref $el eq 'ARRAY' ? 
                    $el : $f->();
    }
    return @res;
}

my %macros;

###############################################################################

# Default macro definitions start

###############################################################################

def_macro('Xo',  sub { @_ }, concat_until => '.Xc');

def_macro('.Ns', sub {ns, @_});
def_macro('Ns',  sub {ns, @_});

{
    my %reference;
    def_macro('.Rs', sub { () } );
    def_macro('.%A', sub {
        if ($reference{authors}) {
            $reference{authors} .= " and @_"
        }
        else {
            $reference{authors} = "@_";
        }
        return ();
    });
    def_macro('.%T', sub { $reference{title} = "@_"; () } );
    def_macro('.%O', sub { $reference{optional} = "@_"; () } );

    sub set_Re_callback {
        my ($sub) = @_;
        croak 'Not a CODE reference' if not ref $sub eq 'CODE';
        def_macro('.Re', sub { 
            my @ret = $sub->(\%reference);
            %reference = (); @ret
        });
        return;
    }
}

def_macro('.Bl', sub { die '.Bl - no list callback set' });
def_macro('.It', sub { die ".It called outside of list context - maybe near line $." });
def_macro('.El', sub { die '.El requires .Bl first' });


{ 
    my $elcb = sub { () };

    sub set_El_callback {
        my ($sub) = @_;
        croak 'Not a CODE reference' if ref $sub ne 'CODE';
        $elcb = $sub;
        return;
    }

    sub set_Bl_callback {
        my ($blcb, %defs) = @_;
        croak 'Not a CODE reference' if ref $blcb ne 'CODE';
        def_macro('.Bl', sub { 

            my $orig_it   = get_macro('.It');
            my $orig_el   = get_macro('.El');
            my $orig_bl   = get_macro('.Bl');
            my $orig_elcb = $elcb;

            # Restore previous .It and .El on each .El
            def_macro('.El', sub {
                    def_macro('.El', delete $orig_el->{run}, %$orig_el);
                    def_macro('.It', delete $orig_it->{run}, %$orig_it);
                    def_macro('.Bl', delete $orig_bl->{run}, %$orig_bl);
                    my @ret = $elcb->(@_);
                    $elcb = $orig_elcb;
                    @ret
                });
            $blcb->(@_) 
        }, %defs);
        return;
    }
}

def_macro('.Sm', sub { 
    my ($arg) = @_;
    if (defined $arg) {
        space($arg);
    } else {
        space() eq 'off' ? 
            space('on') : 
            space('off'); 
    }
    () 
} );
def_macro('Sm', do { my $off; sub { 
    my ($arg) = @_;
    if (defined $arg && $arg =~ /^(on|off)$/) {
        shift;
        if    ($arg eq 'off') { soff, @_; }
        elsif ($arg eq 'on')  { son, @_; }
    }
    else {
        stoggle, @_;
    }
}} );

###############################################################################

# Default macro definitions end

###############################################################################

sub def_macro {
    croak 'Odd number of elements for hash argument <'.(scalar @_).'>' if @_%2;
    my ($macro, $sub, %def) = @_;
    croak 'Not a CODE reference' if ref $sub ne 'CODE';

    $macros{ $macro } = { 
        run          => $sub,
        greedy       => delete $def{greedy} || 0,
        raw          => delete $def{raw}    || 0,
        concat_until => delete $def{concat_until},
    };
    if ($macros{ $macro }{concat_until}) {
        $macros{ $macros{ $macro }{concat_until} } = { run => sub { @_ } };
        $macros{ $macro }{greedy}                  = 1;
    }
    return;
}

sub get_macro {
    my ($macro) = @_;
    croak "Macro <$macro> not defined" if not exists $macros{ $macro };
    +{ %{ $macros{ $macro } } }
}

#TODO: document this
sub parse_opts {
    my %args;
    my $last;
    for (@_) {
        if ($_ =~ /^\\?-/) {
            s/^\\?-//;
            $args{$_} = 1;
            $last = _unquote($_);
        }
        else {
            $args{$last} = _unquote($_) if $last;
            undef $last;
        }
    }
    return %args;
}

sub _is_control {
    my ($el, $expected) = @_;
    if (defined $expected) {
        ref $el eq 'ARRAY' and $el->[0] eq $expected;
    }
    else {
        ref $el eq 'ARRAY';
    }
}

{
    my $sep = ' ';

    sub to_string {
        if (@_ > 0) { 
            # Handle punctunation
            my ($in_brace, @punct) = '';
            my @new = map {
                if (/^([\[\(])$/) {
                    ($in_brace = $1) =~ tr/([/)]/;
                    $_, ns
                }
                elsif (/^([\)\]])$/ && $in_brace eq $1) {
                    $in_brace = '';
                    ns, $_
                }
                elsif ($_ =~ /^[,\.;:\?\!\)\]]$/) {
                    push @punct, ns, $_;
                    ();
                }
                elsif (_is_control($_, 'pp')) {
                    $_->[1]
                }
                elsif (_is_control($_)) {
                    $_
                }
                else {
                    splice (@punct), $_;
                }
            } @_;
            push @new, @punct;

            # Produce string out of an array dealing with the special control characters
            # space('off') must but one character delayed
            my ($no_space, $space_off) = 1;
            my $res = '';
            while (defined(my $el = shift @new)) {
                if    (_is_control($el, 'hardspace'))   { $no_space = 1; $res .= ' ' }
                elsif (_is_control($el, 'nospace'))     { $no_space = 1;             }
                elsif (_is_control($el, 'spaceoff'))    { $space_off = 1;            }
                elsif (_is_control($el, 'spaceon'))     { space('on');               }
                elsif (_is_control($el, 'spacetoggle')) { space() eq 'on' ? 
                                                            $space_off = 1 : 
                                                            space('on')              }
                else {
                    if ($no_space) {
                        $no_space = 0;
                        $res .= "$el"
                    }
                    else {
                        $res .= "$sep$el"
                    }

                    if ($space_off)    { space('off'); $space_off = 0; }
                }
            }
            $res
        }
        else { 
            '';
        }
    }

    sub space {
        my ($arg) = @_;
        if (defined $arg && $arg =~ /^(on|off)$/) {
            $sep = ' ' if $arg eq 'on';
            $sep = ''  if $arg eq 'off';
            return;
        }
        else {
            return $sep eq '' ? 'off' : 'on';
        }
    }
}

sub _unquote {
    my @args = @_;
    $_ =~ s/^"([^"]+)"$/$1/g for @args;
    wantarray ? @args : $args[0];
}

sub call_macro {
    my ($macro, @args) = @_;
    my @ret; 

    my @newargs;
    my $i = 0;

    @args = _unquote(@args) if (!$macros{ $macro }{raw});

    # Call any callable macros in the argument list
    for (@args) {
        if ($_ =~ /^[A-Z][a-z]+$/ && exists $macros{ $_ }) {
            push @ret, call_macro($_, @args[$i+1 .. $#args]);
            last;
        } else {
            if ($macros{ $macro }{greedy}) {
                push @ret, $_;
            }
            else {
                push @newargs, $_;
            }
        }
        $i++;
    }

    if ($macros{ $macro }{concat_until}) {
        my ($n_macro, @n_args) = ('');
        while (1) {
            die "EOF was reached and no $macros{ $macro }{concat_until} found" 
                if not defined $n_macro;
            ($n_macro, @n_args) = parse_line(undef, sub { push @ret, shift });
            if ($n_macro eq $macros{ $macro }{concat_until}) {
                push @ret, call_macro($n_macro, @n_args);
                last;
            }
            else {
                $n_macro =~ s/^\.//;
                push @ret, call_macro($n_macro, @n_args) if exists $macros{ $n_macro };
            }
        }
    }

    if ($macros{ $macro }{greedy}) {
        #print "MACROG $macro (", (join ', ', @ret), ")\n";
        return $macros{ $macro }{run}->(@ret);
    }
    else {
        #print "MACRO $macro (", (join ', ', @newargs), ")".(join ', ', @ret)."\n";
        return $macros{ $macro }{run}->(@newargs), @ret;
    }
}

{
    my ($in_fh, $out_sub, $preprocess_sub);
    sub parse_line {
        $in_fh          = $_[0] if defined $_[0] || !defined $in_fh;
        $out_sub        = $_[1] if defined $_[1] || !defined $out_sub;
        $preprocess_sub = $_[2] if defined $_[2] || !defined $preprocess_sub;

        croak 'out_sub not a CODE reference' 
            if not ref $out_sub eq 'CODE';
        croak 'preprocess_sub not a CODE reference' 
            if defined $preprocess_sub && not ref $preprocess_sub eq 'CODE';

        while (my $line = <$in_fh>) {
            chomp $line;
            if ($line =~ /^\.[A-z][a-z0-9]+/ || $line =~ /^\.%[A-Z]/ || 
                $line =~ /^\.\\"/) 
            {
                $line =~ s/ +/ /g;
                my ($macro, @args) = quotewords(' ', 1, $line);
                @args = grep { defined $_ } @args;
                $preprocess_sub->(@args) if defined $preprocess_sub;
                if ($macro && exists $macros{ $macro }) {
                    return ($macro, @args);
                } else {
                    $out_sub->($line);
                }
            }
            else {
                $out_sub->($line);
            }
        }
        return;
    }
}

1;
__END__
