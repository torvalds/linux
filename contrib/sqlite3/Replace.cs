/*
** 2016 February 26
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains C# code to perform regular expression replacements
** using the standard input and output channels.
*/

using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;

///////////////////////////////////////////////////////////////////////////////

#region Assembly Metadata
[assembly: AssemblyTitle("Replace Tool")]
[assembly: AssemblyDescription("Replace text using standard input/output.")]
[assembly: AssemblyCompany("SQLite Development Team")]
[assembly: AssemblyProduct("SQLite")]
[assembly: AssemblyCopyright("Public Domain")]
[assembly: ComVisible(false)]
[assembly: Guid("95a0513f-8863-48cd-a76f-cb80868cb578")]
[assembly: AssemblyVersion("1.0.*")]

#if DEBUG
[assembly: AssemblyConfiguration("Debug")]
#else
[assembly: AssemblyConfiguration("Release")]
#endif
#endregion

///////////////////////////////////////////////////////////////////////////////

namespace Replace
{
    /// <summary>
    /// This enumeration is used to represent all the possible exit codes from
    /// this tool.
    /// </summary>
    internal enum ExitCode
    {
        /// <summary>
        /// The file download was a success.
        /// </summary>
        Success = 0,

        /// <summary>
        /// The command line arguments are missing (i.e. null).  Generally,
        /// this should not happen.
        /// </summary>
        MissingArgs = 1,

        /// <summary>
        /// The wrong number of command line arguments was supplied.
        /// </summary>
        WrongNumArgs = 2,

        /// <summary>
        /// The "matchingOnly" flag could not be converted to a value of the
        /// <see cref="Boolean"/> type.
        /// </summary>
        BadMatchingOnlyFlag = 3,

        /// <summary>
        /// An exception was caught in <see cref="Main" />.  Generally, this
        /// should not happen.
        /// </summary>
        Exception = 4
    }

    ///////////////////////////////////////////////////////////////////////////

    internal static class Replace
    {
        #region Private Support Methods
        /// <summary>
        /// This method displays an error message to the console and/or
        /// displays the command line usage information for this tool.
        /// </summary>
        /// <param name="message">
        /// The error message to display, if any.
        /// </param>
        /// <param name="usage">
        /// Non-zero to display the command line usage information.
        /// </param>
        private static void Error(
            string message,
            bool usage
            )
        {
            if (message != null)
                Console.WriteLine(message);

            string fileName = Path.GetFileName(
                Process.GetCurrentProcess().MainModule.FileName);

            Console.WriteLine(String.Format(
                "usage: {0} <regExPattern> <regExSubSpec> <matchingOnly>",
                fileName));
        }
        #endregion

        ///////////////////////////////////////////////////////////////////////

        #region Program Entry Point
        /// <summary>
        /// This is the entry-point for this tool.  It handles processing the
        /// command line arguments, reading from the standard input channel,
        /// replacing any matching lines of text, and writing to the standard
        /// output channel.
        /// </summary>
        /// <param name="args">
        /// The command line arguments.
        /// </param>
        /// <returns>
        /// Zero upon success; non-zero on failure.  This will be one of the
        /// values from the <see cref="ExitCode" /> enumeration.
        /// </returns>
        private static int Main(
            string[] args
            )
        {
            //
            // NOTE: Sanity check the command line arguments.
            //
            if (args == null)
            {
                Error(null, true);
                return (int)ExitCode.MissingArgs;
            }

            if (args.Length != 3)
            {
                Error(null, true);
                return (int)ExitCode.WrongNumArgs;
            }

            try
            {
                //
                // NOTE: Create a regular expression from the first command
                //       line argument.  Then, grab the replacement string,
                //       which is the second argument.
                //
                Regex regEx = new Regex(args[0]);
                string replacement = args[1];

                //
                // NOTE: Attempt to convert the third argument to a boolean.
                //
                bool matchingOnly;

                if (!bool.TryParse(args[2], out matchingOnly))
                {
                    Error(null, true);
                    return (int)ExitCode.BadMatchingOnlyFlag;
                }

                //
                // NOTE: Grab the standard input and output channels from the
                //       console.
                //
                TextReader inputTextReader = Console.In;
                TextWriter outputTextWriter = Console.Out;

                //
                // NOTE: Loop until end-of-file is hit on the standard input
                //       stream.
                //
                while (true)
                {
                    //
                    // NOTE: Read a line from the standard input channel.  If
                    //       null is returned here, there is no more input and
                    //       we are done.
                    //
                    string inputLine = inputTextReader.ReadLine();

                    if (inputLine == null)
                        break;

                    //
                    // NOTE: Perform regular expression replacements on this
                    //       line, if any.  Then, write the modified line to
                    //       the standard output channel.
                    //
                    string outputLine = regEx.Replace(inputLine, replacement);

                    if (!matchingOnly || !String.Equals(
                            inputLine, outputLine, StringComparison.Ordinal))
                    {
                        outputTextWriter.WriteLine(outputLine);
                    }
                }

                //
                // NOTE: At this point, everything has succeeded.
                //
                return (int)ExitCode.Success;
            }
            catch (Exception e)
            {
                //
                // NOTE: An exception was caught.  Report it via the console
                //       and return failure.
                //
                Error(e.ToString(), false);
                return (int)ExitCode.Exception;
            }
        }
        #endregion
    }
}
