.. SPDX-License-Identifier: GPL-2.0

Aplicando Patches ao Kernel Linux
+++++++++++++++++++++++++++++++++

Autor Original:
    Jesper Juhl, Agosto de 2005

.. note::

   Este documento está obsoleto. Na maioria dos casos, em vez de usar ``patch``
   manualmente, você quase certamente desejará considerar o uso do Git.

Uma pergunta feita com frequência na Linux Kernel Mailing List é como aplicar
an patch ao kernel ou, mais especificamente, a qual kernel base um patch para
uma das muitas árvores/branches deve ser aplicado. Esperamos que este documento
explique isso a você.

Além de explicar como aplicar e reverter patches, uma breve descrição das
diferentes árvores do kernel (e exemplos de como aplicar seus patches
específicos) também é fornecida.


O que é um Patch?
=================

Um patch é um pequeno documento de texto que contém uma diferença (delta) de
alterações entre duas versões diferentes de uma árvore de código-fonte. Os
patches são criados com o programa ``diff``.

Para aplicar um patch corretamente, você precisa saber de qual base ele foi
gerado e em qual nova versão o patch transformará a árvore de código-fonte.
Ambas as informações devem estar presentes nos metadados do arquivo de patch
ou ser possíveis de deduzir a partir do nome do arquivo.


Como eu aplico ou reverto um patch?
===================================

Você aplica um patch com o programa ``patch``. O programa patch lê um arquivo
de diff (ou patch) e faz as alterações descritas nele na árvore de
código-fonte.

Os patches para o kernel Linux são gerados relativamente ao diretório pai que
contém o diretório do código-fonte do kernel.

Isso significa que os caminhos para os arquivos dentro do arquivo de patch
contêm o nome dos diretórios do código-fonte do kernel contra os quais ele foi
gerado (ou alguns outros nomes de diretório como "a/" e "b/").

Como é improvável que isso corresponda ao nome do diretório do código-fonte do
kernel na sua máquina local (mas frequentemente é uma informação útil para ver
contra qual versão um patch sem identificação foi gerado), você deve entrar no
seu diretório de código-fonte do kernel e, em seguida, remover o primeiro
elemento do caminho dos nomes de arquivos no arquivo de patch ao aplicá-lo (o
argumento ``-p1`` para o ``patch`` faz isso).

Para reverter um patch aplicado anteriormente, use o argumento -R para o patch.
Portanto, se você aplicou um patch desta forma::

    patch -p1 < ../patch-x.y.z

Você pode revertê-lo (desfazê-lo) assim::

    patch -R -p1 < ../patch-x.y.z


Como eu passo um arquivo de patch/diff para o ``patch``?
========================================================

Isso (como de costume no Linux e em outros sistemas operacionais do tipo UNIX)
pode ser feito de várias maneiras diferentes.

Em todos os exemplos abaixo, eu passo o arquivo (em formato não compactado) para
o patch via stdin usando a seguinte sintaxe::

    patch -p1 < path/to/patch-x.y.z

Se você quer apenas ser capaz de seguir os exemplos abaixo e não deseja
conhecer mais do que uma maneira de usar o patch, então você pode parar a
leitura desta seção aqui.

O patch também pode receber o nome do arquivo a ser usado através do argumento
-i, desta forma::

    patch -p1 -i path/to/patch-x.y.z

Se o seu arquivo de patch estiver compactado com gzip ou xz e você não quiser
descompactá-lo antes de aplicá-lo, você pode passá-lo para o patch desta outra
forma::

    xzcat path/to/patch-x.y.z.xz | patch -p1
    bzcat path/to/patch-x.y.z.gz | patch -p1

Se você deseja descompactar o arquivo de patch manualmente primeiro antes de
aplicá-lo (o que presumo que você tenha feito nos exemplos abaixo), basta
executar gunzip ou xz no arquivo -- desta forma::

    gunzip patch-x.y.z.gz
    xz -d patch-x.y.z.xz

O que deixará você com um arquivo patch-x.y.z em texto puro que você pode
passar para o patch via stdin ou pelo argumento ``-i``, conforme sua preferência.

Alguns outros argumentos úteis para o patch são ``-s``, que faz com que o patch
seja silencioso (exceto por erros), o que é bom para evitar que erros sumam da
tela rolando rápido demais; e ``--dry-run``, que faz com que o patch apenas
imprima uma lista do que aconteceria, mas sem realizar nenhuma alteração de
fato. Por fim, ``--verbose`` diz ao patch para imprimir mais informações sobre o
trabalho que está sendo realizado.


Erros comuns ao aplicar patches
===============================

Quando o patch aplica um arquivo de patch, ele tenta verificar a integridade do
arquivo de diferentes maneiras.

Verificar se o arquivo parece um arquivo de patch válido e checar se o código ao
redor dos trechos sendo modificados corresponde ao contexto fornecido no patch
são apenas duas das verificações básicas de integridade que o patch faz.

Se o patch encontrar algo que não pareça totalmente correto, ele tem duas
opções. Ele pode se recusar a aplicar as alterações e abortar, ou pode tentar
encontrar uma maneira de fazer o patch ser aplicado com algumas pequenas
alterações.

Um exemplo de algo que não está "totalmente correto" e que o patch tentará
corrigir é se todo o contexto coincidir, as linhas sendo alteradas coincidirem,
mas os números das linhas forem diferentes. Isso pode acontecer, por exemplo, se
o patch fizer uma alteração no meio do arquivo, mas, por algum motivo, algumas
linhas tiverem sido adicionadas ou removidas perto do início do arquivo. Nesse
caso, tudo parece correto, apenas mudou um pouco para cima ou para baixo, e o
patch geralmente ajustará os números das linhas e aplicará o patch.

Sempre que o patch aplicar um patch que ele teve de modificar um pouco para
fazer caber, ele avisará você dizendo que o patch foi aplicado com **fuzz**.
Você deve ser cauteloso com tais alterações porque, embora o patch
provavelmente tenha acertado, ele nem /sempre/ acerta, e o resultado às vezes
será incorreto.

Quando o patch encontra uma alteração que não consegue corrigir com fuzz, ele a
rejeita imediatamente e deixa um arquivo com a extensão ``.rej`` (um arquivo de
rejeição). Você pode ler esse arquivo para ver exatamente qual alteração não
pôde ser aplicada, para que possa corrigi-la manualmente, se desejar.

Se você não tem nenhum patch de terceiros aplicado ao seu código-fonte do
kernel, mas apenas patches do kernel.org, e você aplica os patches na ordem
correta, e não fez nenhuma modificação por conta própria nos arquivos de
origem, então você nunca deveria ver uma mensagem de fuzz ou de rejeição (reject)
do patch. Se você ainda assim vir tais mensagens, então há um alto risco de que
sua árvore de código-fonte local ou o arquivo de patch estejam corrompidos de
alguma forma. Nesse caso, você provavelmente deveria tentar baixar o patch
novamente e, se as coisas ainda não estiverem certas, aconselha-se começar com
uma árvore limpa baixada na íntegra do kernel.org.

Vamos examinar um pouco mais algumas das mensagens que o patch pode produzir.

Se o patch parar e apresentar um prompt ``File to patch:``, então o patch não
conseguiu encontrar um arquivo para ser modificado. O mais provável é que você
tenha esquecido de especificar -p1 ou esteja no diretório errado. Com menos
frequência, você encontrará patches que precisam ser aplicados com ``-p0`` em
vez de ``-p1`` (a leitura do arquivo de patch deve revelar se este é o caso -- se
for, isso é um erro da pessoa que criou o patch, mas não é fatal).

Se você receber ``Hunk #2 succeeded at 1887 with fuzz 2 (offset 7 lines).`` ou
uma mensagem semelhante a essa, significa que o patch teve que ajustar o local
da alteração (neste exemplo, ele precisou se mover 7 linhas de onde esperava
fazer a alteração para fazê-la caber).

O arquivo resultante pode ou não estar correto, dependendo do motivo pelo qual o
arquivo estava diferente do esperado.

Isso geralmente acontece se você tentar aplicar un patch que foi gerado contra uma
versão de kernel diferente daquela que você está tentando modificar.

Se você receber uma mensagem como ``Hunk #3 FAILED at 2387.``, significa que o
patch não pôde ser aplicado corretamente e o programa patch não foi capaz de
encontrar um caminho usando o fuzz. Isso gerará um arquivo ``.rej`` com a
alteração que fez o patch falhar e também um arquivo ``.orig`` mostrando o
conteúdo original que não pôde ser alterado.

Se você receber ``Reversed (or previously applied) patch detected!  Assume -R? [n]``
então o patch detectou que a alteração contida no patch parece já ter sido feita.

Se você realmente aplicou este patch anteriormente e apenas o reaplicou por erro,
basta dizer [n]ão (n) e abortar este patch. Se você aplicou este patch
anteriormente e realmente pretendia revertê-lo, mas esqueceu de especificar -R,
você pode dizer [**y**]es (sim) aqui para fazer o patch revertê-lo para você.

Isso também pode acontecer se o criador do patch inverteu os diretórios de
origem e destino ao criar o patch e, nesse caso, reverter o patch irá, na
verdade, aplicá-lo.

Uma mensagem semelhante a ``patch: **** unexpected end of file in patch`` ou
``patch unexpectedly ends in middle of line`` significa que o patch não conseguiu
fazer sentido do arquivo que você passou para ele. Ou o seu download está
quebrado, ou você tentou passar para o patch um arquivo de patch compactado sem
descompactá-lo primeiro, ou o arquivo de patch que você está usando foi alterado
por um cliente de e-mail ou agente de transferência de e-mail em algum lugar pelo
caminho, por exemplo, dividindo uma linha longa em duas linhas. Frequentemente,
esses avisos podem ser corrigidos facilmente juntando (concatenando) as duas
linhas que foram divididas.

Como já mencionei acima, esses erros nunca deveriam acontecer se você aplicar um
patch do kernel.org na versão correta de uma árvore de código-fonte não
modificada. Portanto, se você obtiver esses erros com patches do kernel.org,
você provavelmente deve assumir que o seu arquivo de patch ou a sua árvore está
quebrada, e eu o aconselharia a recomeçar com um download limpo de uma árvore
completa do kernel e do patch que deseja aplicar.

Existem alternativas ao ``patch``?
==================================

Sim, existem alternativas.

Você pode usar o programa ``interdiff`` (http://cyberelk.net/tim/patchutils/) para
gerar um patch que represente as diferenças entre dois patches e, em seguida,
aplicar o resultado.

Isso permitirá que você passe de algo como 5.7.2 para 5.7.3 em um único
passo. A flag -z do interdiff permite até mesmo passar patches em formato
compactado com gzip ou bzip2 diretamente, sem o uso de zcat, bzcat ou
descompactação manual.

Aqui está como você passaria de 5.7.2 para 5.7.3 em um único passo::

    interdiff -z ../patch-5.7.2.gz ../patch-5.7.3.gz | patch -p1

Embora o interdiff possa economizar um ou dois passos, geralmente recomenda-se
realizar os passos adicionais, já que o interdiff pode errar em alguns casos.

Outra alternativa é o ``ketchup``, que é um script em python para download e
aplicação automática de patches (https://www.selenic.com/ketchup/).

Outras ferramentas úteis são o diffstat, que mostra um resumo das alterações
feitas por um patch; o lsdiff, que exibe uma lista curta dos arquivos afetados
em um arquivo de patch, junto com (opcionalmente) os números das linhas de
início de cada patch; e o grepdiff, que exibe uma lista dos arquivos modificados
por um patch onde o patch contém uma determinada expressão regular.


Onde posso baixar os patches?
=============================

Os patches estão disponíveis em https://kernel.org/
Os patches mais recentes estão vinculados na página principal, mas eles também
possuem locais específicos.

Os patches 5.x.y (-stable) e 5.x residem em

    https://www.kernel.org/pub/linux/kernel/v5.x/

Os patches incrementais 5.x.y residem em

    https://www.kernel.org/pub/linux/kernel/v5.x/incr/

Os patches -rc não são armazenados no servidor web, mas são gerados sob
demanda a partir de tags do git, tais como

    https://git.kernel.org/torvalds/p/v5.1-rc1/v5.0

Os patches estáveis -rc residem em

    https://www.kernel.org/pub/linux/kernel/v5.x/stable-review/


Os kernels 5.x
==============

Estes são os lançamentos estáveis base publicados por Linus. O lançamento com o
número mais alto é o mais recente.

Se regressões ou outras falhas graves forem encontradas, um patch de correção
-stable será lançado (veja abaixo) sobre esta base. Assim que um novo kernel
base 5.x é lançado, um patch é disponibilizado contendo o delta entre o kernel
5.x anterior e o novo.

Para aplicar um patch mudando da versão 5.6 para a 5.7, você faria o seguinte
(note que tais patches **NÃO** se aplicam sobre kernels 5.x.y, mas sim sobre o
kernel base 5.x -- se você precisar mudar de 5.x.y para 5.x+1, você deve
primeiro reverter o patch do 5.x.y).

Aqui estão alguns exemplos::

    # mudando de 5.6 para 5.7

    $ cd ~/linux-5.6            # muda para o dir do fonte do kernel
    $ patch -p1 < ../patch-5.7      # aplica o patch do 5.7
    $ cd ..
    $ mv linux-5.6 linux-5.7        # renomeia o dir do fonte

    # mudando de 5.6.1 para 5.7

    $ cd ~/linux-5.6.1          # muda para o dir do fonte do kernel
    $ patch -p1 -R < ../patch-5.6.1     # reverte o patch do 5.6.1
                        # o dir do fonte agora é o 5.6
    $ patch -p1 < ../patch-5.7      # aplica o novo patch do 5.7
    $ cd ..
    $ mv linux-5.6.1 linux-5.7      # renomeia o dir do fonte

Os kernels 5.x.y
================

Kernels com versões de 3 dígitos são kernels -stable (estáveis). Eles contêm
correções críticas relativamente pequenas para problemas de segurança ou
regressões significativas descobertas em um determinado kernel 5.x.

Esta é a ramificação recomendada para usuários que desejam o kernel estável mais
recente e não estão interessados em ajudar a testar versões de desenvolvimento
ou experimentais.

Se nenhum kernel 5.x.y estiver disponível, então o kernel 5.x com o número mais
alto será o atual kernel estável.

A equipe -stable fornece patches normais, bem como incrementais. Abaixo está
como aplicar esses patches.

Patches normais
~~~~~~~~~~~~~~~

Estes patches não são incrementais, o que significa que, por exemplo, o patch
5.7.3 não se aplica sobre o código-fonte do kernel 5.7.2, mas sim sobre o
código-fonte do kernel base 5.7.

Portanto, para aplicar o patch 5.7.3 ao seu código-fonte existente do kernel
5.7.2, você deve primeiro remover o patch 5.7.2 (de modo que reste apenas o
código-fonte do kernel base 5.7) e então aplicar o novo patch 5.7.3.

Aqui está um pequeno exemplo::

    $ cd ~/linux-5.7.2          # muda para o dir do fonte do kernel
    $ patch -p1 -R < ../patch-5.7.2     # reverte o patch do 5.7.2
    $ patch -p1 < ../patch-5.7.3        # aplica o novo patch do 5.7.3
    $ cd ..
    $ mv linux-5.7.2 linux-5.7.3        # renomeia o dir do fonte do kernel

Patches incrementais
~~~~~~~~~~~~~~~~~~~~

Os patches incrementais são diferentes: em vez de serem aplicados sobre o kernel
base 5.x, eles são aplicados sobre o kernel estável anterior (5.x.y-1).

Aqui está o exemplo para aplicar estes::

    $ cd ~/linux-5.7.2          # muda para o dir do fonte do kernel
    $ patch -p1 < ../patch-5.7.2-3      # aplica o novo patch do 5.7.3
    $ cd ..
    $ mv linux-5.7.2 linux-5.7.3        # renomeia o dir do fonte do kernel


Os kernels -rc
==============

Estes são os kernels candidatos a lançamento (release-candidate). São kernels
de desenvolvimento publicados por Linus sempre que ele considera que a árvore
atual do git (a ferramenta de gerenciamento de código-fonte do kernel) está em
um estado razoavelmente íntegro e adequado para testes.

Estes kernels não são estáveis e você deve esperar quebras ocasionais se pretender
executá-los. Esta é, no entanto, a mais estável das principais ramificações de
desenvolvimento e é também o que eventualmente se tornará o próximo kernel
estável, por isso é importante que seja testado pelo maior número possível de
pessoas.

Esta é uma boa ramificação para pessoas que querem ajudar a testar kernels de
desenvolvimento, mas não querem executar algumas das coisas realmente
experimentais (essas pessoas devem ver as seções sobre os kernels -next e -mm
abaixo).

Os patches -rc não são incrementais; eles se aplicam a um kernel base 5.x, assim
como os patches 5.x.y descritos acima. A versão do kernel antes do sufixo -rcN
indica a versão do kernel na qual este kernel -rc eventualmente se tornará.

Portanto, 5.8-rc5 significa que este é o quinto candidato a lançamento para o
kernel 5.8 e o patch deve ser aplicado sobre o código-fonte do kernel 5.7.

Aqui estão 3 exemplos de como aplicar esses patches::

    # primeiro, um exemplo de mudança do 5.7 para o 5.8-rc3

    $ cd ~/linux-5.7            # muda para o dir do fonte do 5.7
    $ patch -p1 < ../patch-5.8-rc3      # aplica o patch do 5.8-rc3
    $ cd ..
    $ mv linux-5.7 linux-5.8-rc3        # renomeia o dir do fonte

    # agora vamos mudar do 5.8-rc3 para o 5.8-rc5

    $ cd ~/linux-5.8-rc3            # muda para o dir do 5.8-rc3
    $ patch -p1 -R < ../patch-5.8-rc3   # reverte o patch do 5.8-rc3
    $ patch -p1 < ../patch-5.8-rc5      # aplica o novo patch do 5.8-rc5
    $ cd ..
    $ mv linux-5.8-rc3 linux-5.8-rc5    # renomeia o dir do fonte

    # por fim, vamos tentar mudar do 5.7.3 para o 5.8-rc5

    $ cd ~/linux-5.7.3          # muda para o dir do fonte do kernel
    $ patch -p1 -R < ../patch-5.7.3     # reverte o patch do 5.7.3
    $ patch -p1 < ../patch-5.8-rc5      # aplica o novo patch do 5.8-rc5
    $ cd ..
    $ mv linux-5.7.3 linux-5.8-rc5      # renomeia o dir do fonte do kernel


Os patches -mm e a árvore linux-next
====================================

Os patches -mm são patches experimentais publicados por Andrew Morton.

No passado, a árvore -mm também era usada para testar patches de subsistemas,
mas essa função agora é realizada por meio da árvore
`linux-next` (https://www.kernel.org/doc/man-pages/linux-next.html).
Os mantenedores de subsistemas enviam seus patches primeiro para a linux-next e,
durante a janela de mesclagem (merge window), enviam-nos diretamente para Linus.

Os patches -mm servem como uma espécie de campo de testes para novos recursos e
outros patches experimentais que não são mesclados por meio de uma árvore de
subsistema. Assim que tais patches provam seu valor na -mm por um tempo, Andrew
os envia para Linus para inclusão na linha principal (mainline).

A árvore linux-next é atualizada diariamente e inclui os patches -mm. Ambas
estão em constante fluxo e contêm muitos recursos experimentais, uma grande
quantidade de patches de depuração (debugging) não apropriados para a linha
principal etc., sendo as mais experimentais das ramificações descritas neste
documento.

Estes patches não são apropriados para uso em sistemas que devem ser estáveis e
são mais arriscados de executar do que qualquer uma das outras ramificações
(certifique-se de ter backups atualizados -- isso vale para qualquer kernel
experimental, mas ainda mais para patches -mm ou ao usar um kernel da árvore
linux-next).

O teste dos patches -mm e da linux-next é imensamente apreciado, pois todo o
objetivo deles é eliminar regressões, travamentos (crashes), bugs de corrupção
de dados, quebras de compilação (e qualquer outro bug em geral) antes que as
alterações sejam mescladas na árvore principal do Linus, que é mais estável.

Mas os testadores da -mm e da linux-next devem estar cientes de que quebras são
mais comuns do que em qualquer outra árvore.


Isso conclui esta lista de explicações sobre as várias árvores do kernel.
Espero que agora você tenha clareza sobre como aplicar os vários patches e
ajudar a testar o kernel.

Agradecimentos a Randy Dunlap, Rolf Eike Beer, Linus Torvalds, Bodo Eggert,
Johannes Stezenbach, Grant Coady, Pavel Machek e outros que posso ter esquecido
por suas revisões e contribuições para este documento.