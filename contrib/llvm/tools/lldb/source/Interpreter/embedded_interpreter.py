import sys
if sys.version_info[0] < 3:
    import __builtin__ as builtins
else:
    import builtins
import code
import lldb
import traceback

try:
    import readline
    import rlcompleter
except ImportError:
    have_readline = False
except AttributeError:
    # This exception gets hit by the rlcompleter when Linux is using
    # the readline suppression import.
    have_readline = False
else:
    have_readline = True
    if 'libedit' in readline.__doc__:
        readline.parse_and_bind('bind ^I rl_complete')
    else:
        readline.parse_and_bind('tab: complete')

g_builtin_override_called = False


class LLDBQuitter(object):

    def __init__(self, name):
        self.name = name

    def __repr__(self):
        self()

    def __call__(self, code=None):
        global g_builtin_override_called
        g_builtin_override_called = True
        raise SystemExit(-1)


def setquit():
    '''Redefine builtin functions 'quit()' and 'exit()' to print a message and raise an EOFError exception.'''
    # This function will be called prior to each interactive
    # interpreter loop or each single line, so we set the global
    # g_builtin_override_called to False so we know if a SystemExit
    # is thrown, we can catch it and tell the difference between
    # a call to "quit()" or "exit()" and something like
    # "sys.exit(123)"
    global g_builtin_override_called
    g_builtin_override_called = False
    builtins.quit = LLDBQuitter('quit')
    builtins.exit = LLDBQuitter('exit')

# When running one line, we might place the string to run in this string
# in case it would be hard to correctly escape a string's contents

g_run_one_line_str = None


def get_terminal_size(fd):
    try:
        import fcntl
        import termios
        import struct
        hw = struct.unpack('hh', fcntl.ioctl(fd, termios.TIOCGWINSZ, '1234'))
    except:
        hw = (0, 0)
    return hw


def readfunc_stdio(prompt):
    sys.stdout.write(prompt)
    return sys.stdin.readline().rstrip()


def run_python_interpreter(local_dict):
    # Pass in the dictionary, for continuity from one session to the next.
    setquit()
    try:
        fd = sys.stdin.fileno()
        interacted = False
        if get_terminal_size(fd)[1] == 0:
            try:
                import termios
                old = termios.tcgetattr(fd)
                if old[3] & termios.ECHO:
                    # Need to turn off echoing and restore
                    new = termios.tcgetattr(fd)
                    new[3] = new[3] & ~termios.ECHO
                    try:
                        termios.tcsetattr(fd, termios.TCSADRAIN, new)
                        interacted = True
                        code.interact(
                            banner="Python Interactive Interpreter. To exit, type 'quit()', 'exit()'.",
                            readfunc=readfunc_stdio,
                            local=local_dict)
                    finally:
                        termios.tcsetattr(fd, termios.TCSADRAIN, old)
            except:
                pass
            # Don't need to turn off echoing
            if not interacted:
                code.interact(
                    banner="Python Interactive Interpreter. To exit, type 'quit()', 'exit()' or Ctrl-D.",
                    readfunc=readfunc_stdio,
                    local=local_dict)
        else:
            # We have a real interactive terminal
            code.interact(
                banner="Python Interactive Interpreter. To exit, type 'quit()', 'exit()' or Ctrl-D.",
                local=local_dict)
    except SystemExit as e:
        global g_builtin_override_called
        if not g_builtin_override_called:
            print('Script exited with %s' % (e))


def run_one_line(local_dict, input_string):
    global g_run_one_line_str
    setquit()
    try:
        repl = code.InteractiveConsole(local_dict)
        if input_string:
            repl.runsource(input_string)
        elif g_run_one_line_str:
            repl.runsource(g_run_one_line_str)

    except SystemExit as e:
        global g_builtin_override_called
        if not g_builtin_override_called:
            print('Script exited with %s' % (e))
