[= AutoGen5 template foo=(base-name) -*- Mode: scheme -*-=]
[=

(emit (dne "# "))

(if (not (and (exist? "prog-name") (exist? "prog-title") (exist? "version")))
    (error "prog-name and prog-title are required"))
(define prog-name (get "prog-name"))

(if (> (string-length prog-name) 16)
    (error (sprintf "prog-name limited to 16 characters:  %s"
           prog-name)) )
(if (not (exist? "long-opts"))
    (error "long-opts is required"))

;; perl list containing string to initialize the option hash
(define perl_opts "")
;; perl list containing option definitions for Getopt::Long
(define perl_defs "       ")
;; usage string
(define perl_usage "")

(define optname-from "A-Z_^")
(define optname-to   "a-z--")
(define counter 0)

(define q (lambda (s) (string-append "'" s "'")))
(define qp (lambda (s) (string-append "q{" s "}")))

=][=

FOR flag =][=

(define optarg "")      ;; the option argument for Getopt::Long
(define opttarget "''") ;; the value of a hash key that represents option
(define optargname "")
(define optisarray #f)
(define optname (string-tr! (get "name") optname-from optname-to))

=][= #
;; since autoopts doesn't support float we take the combination arg-name =
;; float and arg-type = string as float
=][=
  IF arg-type       =][=
    CASE arg-type   =][=

    =* num          =][= (set! optarg "=i") =][=

    =* str          =][=
        (if (and (exist? "arg-name") (== (get "arg-name") "float"))
            (set! optarg "=f")
            (set! optarg "=s")
        )           =][=

    *               =][=
        (error (string-append "unknown arg type '"
        (get "arg-type") "' for " (get "name"))) =][=
    ESAC arg-type   =][=
  ENDIF             =][=

(if (exist? "stack-arg")
    ;; set optarget to array reference if can take more than one value
    ;;  FIXME:  if "max" exists, then just presume it is greater than 1
    ;;
    (if (and (exist? "max") (== (get "max") "NOLIMIT"))
        (begin
          (set! opttarget (string-append
            "["
            (if (exist? "arg-default") (q (get "arg-default")) "")
            "]"
            )
          )
          (set! optisarray #t)
        )
        (error "If stack-arg then max has to be NOLIMIT")
    )
    ;; just scalar otherwise
    (if (exist? "arg-default") (set! opttarget (q (get "arg-default"))))
)

(set! perl_opts (string-append perl_opts
      "'" (get "name") "' => " opttarget ",\n        "))

(define def_add (string-append "'" optname (if (exist? "value")
                  (string-append "|" (get "value")) "") optarg "',"))

(define add_len (+ (string-length def_add) counter))
(if (> add_len 80)
    (begin
      (set! perl_defs (string-append perl_defs "\n        " def_add))
      (set! counter 8)
    )
    (begin
      (set! perl_defs (string-append perl_defs " " def_add))
      (set! counter (+ counter add_len))
    )
)

(if (exist? "arg-type")
    (if (and (exist? "arg-name") (== (get "arg-name") "float"))
        (set! optargname "=float")
        (set! optargname (string-append "=" (substring (get "arg-type") 0 3)))
    )
    (set! optargname "  ")
)

(if (not (exist? "deprecated"))
    (set! perl_usage (string-append perl_usage
       (sprintf "\n    %-28s %s" (string-append
            (if (exist? "value") (string-append "-" (get "value") ",") "   ")
            " --"
            (get "name")
            optargname)
         (get "descrip"))
)   )  )
(if optisarray
  (set! perl_usage (string-append perl_usage
        "\n                                   - may appear multiple times"))
)

=][=

ENDFOR each "flag" =]

use Getopt::Long qw(GetOptionsFromArray);
Getopt::Long::Configure(qw(no_auto_abbrev no_ignore_case_always));

my $usage;

sub usage {
    my ($ret) = @_;
    print STDERR $usage;
    exit $ret;
}

sub paged_usage {
    my ($ret) = @_;
    my $pager = $ENV{PAGER} || '(less || more)';

    open STDOUT, "| $pager" or die "Can't fork a pager: $!";
    print $usage;

    exit $ret;
}

sub processOptions {
    my $args = shift;

    my $opts = {
        [= (. perl_opts) =]'help' => '', 'more-help' => ''
    };
    my $argument = '[= argument =]';
    my $ret = GetOptionsFromArray($args, $opts, (
[= (. perl_defs) =]
        'help|?', 'more-help'));

    $usage = <<'USAGE';
[= prog-name =] - [= prog-title =] - Ver. [= version =]
USAGE: [= prog-name =] [ -<flag> [<val>] | --<name>[{=| }<val>] ]... [= argument =]
[= (. perl_usage)   =]
    -?, --help                   Display usage information and exit
        --more-help              Pass the extended usage information through a pager

Options are specified by doubled hyphens and their name or by a single
hyphen and the flag character.
USAGE

    usage(0)       if $opts->{'help'};
    paged_usage(0) if $opts->{'more-help'};[=

CASE argument       =][=
!E                  =][=
==* "["             =][=
*                   =]

    if ($argument && $argument =~ /^[^\[]/ && !@$args) {
        print STDERR "Not enough arguments supplied (See --help/-?)\n";
        exit 1;
    }[=

ESAC

=]
    $_[0] = $opts;
    return $ret;
}

END { close STDOUT };
